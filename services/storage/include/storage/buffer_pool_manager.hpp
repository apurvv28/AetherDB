#pragma once

#include "storage/disk_manager.hpp"
#include "storage/page.hpp"
#include "storage/replacer.hpp"
#include "common/concurrency.hpp"
#include "common/types.hpp"
#include <unordered_map>
#include <list>
#include <vector>

namespace aether {

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager, Replacer *replacer);
    ~BufferPoolManager();

    // Prevent copy and move
    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;
    BufferPoolManager(BufferPoolManager&&) = delete;
    BufferPoolManager& operator=(BufferPoolManager&&) = delete;

    // Fetch a page from the buffer pool. If not in memory, read from disk.
    // Automatically increments page pin count.
    Page *FetchPage(page_id_t page_id);

    // Unpins a page, letting the replacement policy know it can eventually be evicted.
    // Sets the page's dirty flag to true if is_dirty was true.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Write a page to disk if it is dirty, resetting the dirty flag.
    bool FlushPage(page_id_t page_id);

    // Create a new page, allocating a page ID from DiskManager.
    // Caches it in the buffer pool, pinned (pin count = 1).
    Page *NewPage(page_id_t *page_id);

    // Deallocate/delete a page from both the cache and disk.
    bool DeletePage(page_id_t page_id);

    // Write all dirty pages in the page table to disk.
    void FlushAllPages();

    // Testing and debugging helper methods
    size_t GetPoolSize() const { return pool_size_; }
    int GetPinCount(page_id_t page_id);
    bool IsCached(page_id_t page_id);

private:
    size_t pool_size_;
    DiskManager *disk_manager_;
    Replacer *replacer_;
    Page *pages_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::list<frame_id_t> free_list_;
    mutable Mutex latch_;
};

} // namespace aether
