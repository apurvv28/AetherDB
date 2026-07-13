# AetherDB Documentation: Phase 1 — Disk Manager & Storage Service

This document provides a detailed explanation of the functions, structures, and design decisions behind AetherDB's raw storage substrate and the `AetherStorage` microservice.

---

## 1. Core Data Types (`common/types.hpp`)

We define fundamental types used across the database engine:

- **`page_id_t`** (`uint32_t`): Unique identifier for a page. Corresponds to the page's position offset in the database file: `offset = page_id * PAGE_SIZE`.
- **`lsn_t`** (`int32_t`): Log Sequence Number. Used by the write-ahead logging (WAL) system to track updates.
- **`txn_id_t`** (`uint32_t`): Unique identifier for a transaction.
- **`PAGE_SIZE`** (`4096` bytes): The size of a single physical page on disk. Fits neatly with OS/filesystem sector sizes to minimize sector fragmentation and optimize alignment.
- **`INVALID_PAGE_ID`** (`0xFFFFFFFF`): Sentinel value used to represent a null or unallocated page pointer.

---

## 2. Page Storage Substrate: `DiskManager`

The `DiskManager` (`services/storage/src/disk_manager.cpp`) is responsible for allocating, reading, writing, and reclaiming 4KB chunks of data from a single database file on disk.

```
+-------------------------------------------------------------+
| Page 0 (Metadata Page)                                      |
| [ Free List Head (4B) ] [ Total Page Count (4B) ] [ Zeros ] |
+-------------------------------------------------------------+
| Page 1 (Active User Page)                                   |
| [ Binary Tuple Data... ]                                    |
+-------------------------------------------------------------+
| Page 2 (Deallocated / Free Page)                            |
| [ Next Free Page ID (4B) ] [ Old Tuple Data / Zeros ]       |
+-------------------------------------------------------------+
```

### A. Metadata Layout (Page 0)
The first page of the database file (Page 0) is reserved for system metadata:
- **Bytes 0–3**: `free_list_head` (`page_id_t`) — Points to the first available deallocated page. If no pages are free, it holds `INVALID_PAGE_ID`.
- **Bytes 4–7**: `num_pages` (`uint32_t`) — The total number of pages in the database file (including Page 0).

### B. Free Space Management (Linked List)
Instead of using a complex bitmap that consumes valuable space and requires scanning overhead, AetherDB manages deleted pages via a disk-based linked list:
- When a page (e.g., `page_id`) is deallocated via `DeallocatePage(page_id)`:
  1. We read the current `free_list_head` from Page 0.
  2. We overwrite the first 4 bytes of `page_id` with this head pointer.
  3. We update Page 0's `free_list_head` to point to `page_id`.
  4. This forms an append-only stack of free pages.
- When a new page is allocated via `AllocatePage()`:
  1. If `free_list_head` is valid (`!= INVALID_PAGE_ID`), we pop it.
  2. We read the first 4 bytes of that page to obtain the `next_free_page_id`, and update Page 0's `free_list_head` to it.
  3. We return the popped page ID.
  4. If `free_list_head` is `INVALID_PAGE_ID`, we grow the file size: we increment the `num_pages` counter in Page 0, append 4KB of zeroes to the end of the file, and return the new page ID.

### C. Thread Safety
A `std::mutex` (`db_io_lck_`) synchronizes all disk operations inside the `DiskManager`, protecting file seeking, reading, writing, and metadata updates from concurrent client handler threads.

---

## 3. Communication Layer: `Socket` & `ServerSocket`

To decouple storage and query compute into separate processes, we implemented a cross-platform TCP socket wrapper (`common/src/socket.cpp`):
- **Windows**: Uses Winsock2 (`<winsock2.h>`). On startup, `InitializeNetwork()` invokes `WSAStartup()`, and on shutdown, `CleanupNetwork()` invokes `WSACleanup()`.
- **POSIX (Linux/macOS)**: Map directly to standard BSD socket calls (`socket`, `bind`, `listen`, `accept`, `send`, `recv`).
- **`Socket::Send` / `Socket::Recv`**: Handle partial packet transmissions by wrapping socket calls in loops that ensure the exact requested number of bytes are fully transmitted before returning.

---

## 4. Binary Wire Protocol (`common/protocol.hpp`)

Clients communicate with the storage daemon using a lightweight binary protocol:

### A. Request Format
Every request consists of a 9-byte header, followed by a payload if applicable:
```cpp
struct RequestHeader {
    uint8_t op_type;    // 0: READ_PAGE, 1: WRITE_PAGE, 2: ALLOCATE_PAGE, 3: DEALLOCATE_PAGE
    uint32_t page_id;   // Target page_id (ignored for ALLOCATE)
    uint32_t data_len;  // Length of following payload (e.g. 4096 for WRITE_PAGE, 0 otherwise)
};
```

### B. Response Format
Every response consists of a 9-byte header, followed by a payload if applicable:
```cpp
struct ResponseHeader {
    uint8_t status;     // 0: SUCCESS, 1: ERROR
    uint32_t page_id;   // The page_id (relevant for ALLOCATE_PAGE)
    uint32_t data_len;  // Length of following payload (e.g. 4096 for READ_PAGE, 0 otherwise)
};
```

---

## 5. Storage Service Daemon: `StorageService`

The storage service (`services/storage/src/storage_service.cpp`) operates as a multi-threaded RPC daemon:
1. It listens on port `9001` (by default).
2. Upon receiving a client connection, it spawns a detached worker thread executing `HandleClient`.
3. `HandleClient` reads binary requests from the socket in a loop, calls the corresponding `DiskManager` functions, and writes back the results in the protocol format.
4. If an invalid `page_id` is requested (causing an exception in `DiskManager`), `StorageService` catches the exception and returns a response header with `status = ERROR`, preventing the daemon from crashing.
