# AetherDB Documentation: Phase 2 — Buffer Pool Manager

This document provides a detailed explanation of the in-memory page cache system, the replacement strategies, and the design decisions behind AetherDB's `BufferPoolManager`.

---

## 1. Page Frame Representation: `Page` (`services/storage/include/storage/page.hpp`)

In memory, physical database pages are wrapped inside `Page` frames. A `Page` acts as a container in the buffer pool for one physical page on disk.

```
+-----------------------------------------------------------+
| Page Frame (aether::Page)                                 |
+-----------------------------------------------------------+
| data_ [PAGE_SIZE = 4096 bytes]                            |
| page_id_ (page_id_t)                                      |
| pin_count_ (int)                                          |
| is_dirty_ (bool)                                          |
+-----------------------------------------------------------+
```

### Key Metadata Fields:
- **`data_`** (`char[PAGE_SIZE]`): The actual raw byte contents of the page loaded from disk.
- **`page_id_`** (`page_id_t`): The ID of the physical disk page currently cached in this frame. If the frame is empty/unused, this is set to `INVALID_PAGE_ID`.
- **`pin_count_`** (`int`): The number of concurrent threads/queries currently referencing or using this page. A page with `pin_count_ > 0` **cannot** be evicted from memory.
- **`is_dirty_`** (`bool`): A flag indicating if the page was modified in memory. If true, the page must be written back to disk before its frame can be repurposed or evicted.

---

## 2. Pluggable Eviction Policies: `Replacer` (`services/storage/include/storage/replacer.hpp`)

When the buffer pool is full and a new page needs to be loaded, the database must select an existing in-memory page to evict. The `Replacer` tracks which page frames are currently unpinned (`pin_count == 0`) and eligible for eviction.

AetherDB implements two pluggable replacement algorithms:

### A. LRU (Least-Recently-Used) Replacer
The `LRUReplacer` evicts the frame that was unpinned longest ago.
- **Data Structures**:
  - `std::list<frame_id_t> lru_list_`: A doubly-linked list tracking the order of unpinned frames. The least-recently unpinned frame sits at the front.
  - `std::unordered_map<frame_id_t, list::iterator> lru_map_`: Maps a frame ID to its iterator position in the list for $O(1)$ operations.
- **Mechanics**:
  - `Unpin(frame_id)`: If the frame is not already in the replacer, it is appended to the back of `lru_list_` and recorded in `lru_map_`.
  - `Pin(frame_id)`: If the frame is in the replacer, it is removed from the list and map, making it ineligible for eviction.
  - `Victim(*frame_id)`: Picks the frame at the front of the list, removes it, and returns `true`. Returns `false` if the list is empty.

### B. Clock (Second-Chance) Replacer
The `ClockReplacer` mimics LRU with lower overhead by cycling a clock hand over circular status vectors.
- **Data Structures**:
  - `std::vector<bool> ref_flags_`: Reference flags. A value of `true` means the frame has been accessed recently.
  - `std::vector<bool> in_replacer_`: Tracks whether a frame is currently in the replacer (unpinned).
  - `size_t clock_hand_`: Points to the current frame index being evaluated.
- **Mechanics**:
  - `Unpin(frame_id)`: Sets `in_replacer_[frame_id] = true` and `ref_flags_[frame_id] = true`.
  - `Pin(frame_id)`: Sets `in_replacer_[frame_id] = false`.
  - `Victim(*frame_id)`: The clock hand scans frames. If `in_replacer_[clock_hand_]` is true:
    - If `ref_flags_[clock_hand_]` is true, it is cleared to `false` (giving it a second chance), and the hand advances.
    - If `ref_flags_[clock_hand_]` is false, this frame is chosen as the victim. `in_replacer_` is set to `false`, and the hand advances.

---

## 3. Buffer Pool Manager Core: `BufferPoolManager`

The `BufferPoolManager` (`services/storage/src/buffer_pool_manager.cpp`) coordinates interaction between the file-based `DiskManager` and the in-memory `Page` array.

```
       +---------------------------------------------+
       |             BufferPoolManager               |
       |  +---------------------------------------+  |
       |  | Page Table (page_id -> frame_id)      |  |
       |  +---------------------------------------+  |
       |  | Free List                             |  |
       |  +---------------------------------------+  |
       |  | Eviction Replacer (LRU / Clock)       |  |
       |  +---------------------------------------+  |
       |                                             |
       |      pages_ (contiguous Page array)         |
       |  +-------------+-------------+-----------+  |
       |  | PageFrame 0 | PageFrame 1 |   ...     |  |
       |  +-------------+-------------+-----------+  |
       +---------|-------------|---------------------+
                 |             |
           Read / Write   Page Eviction Flush
                 |             |
        +--------v-------------v---------------------+
        |                 DiskManager                |
        +--------------------------------------------+
```

### Core Operations Walkthrough:

#### A. Fetching a Page (`FetchPage`)
When a component needs to read or write a page, it requests it from the buffer pool:
1. **Search**: Look up `page_id` in the `page_table_`.
   - *Hit*: Increment the page's `pin_count_`, call `replacer_->Pin(frame_id)`, and return the page pointer.
   - *Miss*: Select a target frame for replacement:
     - Check the `free_list_` first.
     - If empty, request a victim frame from `replacer_->Victim(&frame_id)`.
2. **Eviction**: If a victim frame is chosen:
   - Check if the page in that frame is dirty (`IsDirty()`). If so, write it back to disk via `disk_manager_->WritePage` and clear its dirty flag.
   - Erase the evicted `page_id` from the `page_table_`.
3. **Load**: Reset the page metadata, read the requested page data from disk via `disk_manager_->ReadPage`, associate it with the new `page_id`, set `pin_count_ = 1`, and update the `page_table_`.
4. **Pin**: Pin the frame in the replacer to ensure it stays in memory while being used.

#### B. Unpinning a Page (`UnpinPage`)
When a thread is done operating on a page, it must call `UnpinPage`:
1. Decrement the frame's `pin_count_`.
2. If `is_dirty` was passed as `true`, mark the frame dirty (`SetDirty(true)`).
3. If the `pin_count_` drops to `0`, call `replacer_->Unpin(frame_id)` to make it an eviction candidate.

#### C. Creating a New Page (`NewPage`)
Used when expanding the database file (e.g. allocating a new table or index page):
1. Find a target frame (using free list or evicting a victim, flushing if dirty).
2. Allocate a new physical page ID from `disk_manager_->AllocatePage()`.
3. Reset the frame, record the mapping in the `page_table_`, initialize metadata with `pin_count_ = 1`, and Pin the frame in the replacer.

#### D. Deleting a Page (`DeletePage`)
Reclaims a page and returns it to the disk free list:
1. If the page is currently cached and pinned (`pin_count_ > 0`), deletion fails (`return false`).
2. If cached and unpinned: remove the frame from `replacer_`, erase it from `page_table_`, reset its memory, push the frame back to `free_list_`, and call `disk_manager_->DeallocatePage` to reuse the space.
3. If not cached, call `disk_manager_->DeallocatePage` directly.

---

## 4. Concurrency & Thread Safety

To support concurrent request handlers in a multi-threaded TCP database environment:
- The entire `BufferPoolManager` is protected by a mutual exclusion lock (`std::mutex latch_`).
- Every public API method (`FetchPage`, `UnpinPage`, `NewPage`, `DeletePage`, `FlushPage`, `FlushAllPages`) locks the `latch_` on entry and unlocks it on exit via `LockGuard`.
- This ensures that page frame metadata, free list, page table, and replacers remain atomic and race-free.

---

## 5. Storage Daemon Integration (`services/storage/src/storage_service.cpp`)

In Phase 2, the TCP daemon (`AetherStorage`) moves from direct Disk I/O to buffer-pool managed access:
- On startup, the service initializes an `LRUReplacer` and a `BufferPoolManager` with a pool capacity of 128 frames.
- **Client Read Operations**: The client worker thread calls `buffer_pool_manager_->FetchPage(page_id)` to fetch the page frame, sends the cached 4KB payload over TCP, and immediately calls `buffer_pool_manager_->UnpinPage(page_id, false)` to drop its pin.
- **Client Write Operations**: The worker thread fetches the target page frame, copies the incoming TCP payload into `page->GetData()`, and unpins it with `is_dirty = true`. The buffer pool manager will handle persisting the changes asynchronously on eviction or during shutdown, drastically reducing direct write latency.
