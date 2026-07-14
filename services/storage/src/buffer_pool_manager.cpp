#include "storage/buffer_pool_manager.hpp"
#include "common/logger.hpp"

namespace aether {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, Replacer *replacer)
    : pool_size_(pool_size), disk_manager_(disk_manager), replacer_(replacer) {
    pages_ = new Page[pool_size_];
    // Initially, all frames are in the free list
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    LockGuard lock(latch_);
    
    if (page_id == INVALID_PAGE_ID) {
        return nullptr;
    }
    
    // 1. Search the page table
    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        frame_id_t frame_id = iter->second;
        Page *page = &pages_[frame_id];
        page->pin_count_++;
        replacer_->Pin(frame_id);
        spdlog::debug("FetchPage: Page {} hit in frame {}", page_id, frame_id);
        return page;
    }
    
    // 2. Select a replacement frame
    frame_id_t frame_id = -1;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
        spdlog::debug("FetchPage: Page {} miss. Used free frame {}", page_id, frame_id);
    } else {
        if (!replacer_->Victim(&frame_id)) {
            spdlog::warn("FetchPage: Page {} miss. Buffer pool is full and all pages are pinned!", page_id);
            return nullptr;
        }
        // Evict the victim page
        Page *victim_page = &pages_[frame_id];
        page_id_t victim_page_id = victim_page->GetPageId();
        spdlog::debug("FetchPage: Page {} miss. Evicted page {} from frame {}", page_id, victim_page_id, frame_id);
        if (victim_page->IsDirty()) {
            disk_manager_->WritePage(victim_page_id, victim_page->GetData());
            victim_page->SetDirty(false);
            spdlog::debug("FetchPage: Flushed dirty page {} to disk on eviction", victim_page_id);
        }
        page_table_.erase(victim_page_id);
    }
    
    // 3. Load page from disk and update page table
    Page *page = &pages_[frame_id];
    page->ResetMemory();
    page->page_id_ = page_id;
    page->pin_count_ = 1;
    
    disk_manager_->ReadPage(page_id, page->GetData());
    
    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);
    
    return page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    LockGuard lock(latch_);
    
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }
    
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    
    if (page->GetPinCount() <= 0) {
        return false;
    }
    
    if (is_dirty) {
        page->SetDirty(true);
    }
    
    page->pin_count_--;
    if (page->GetPinCount() == 0) {
        replacer_->Unpin(frame_id);
        spdlog::debug("UnpinPage: Page {} pin count reached 0, added frame {} to replacer", page_id, frame_id);
    }
    
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    LockGuard lock(latch_);
    
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }
    
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    
    if (page->IsDirty()) {
        disk_manager_->WritePage(page->GetPageId(), page->GetData());
        page->SetDirty(false);
        spdlog::debug("FlushPage: Flushed page {} to disk", page_id);
    }
    
    return true;
}

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
    LockGuard lock(latch_);
    
    // 1. Find a replacement frame
    frame_id_t frame_id = -1;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
        spdlog::debug("NewPage: Used free frame {}", frame_id);
    } else {
        if (!replacer_->Victim(&frame_id)) {
            spdlog::warn("NewPage: Failed. Buffer pool is full and all pages are pinned!");
            *page_id = INVALID_PAGE_ID;
            return nullptr;
        }
        // Evict the victim page
        Page *victim_page = &pages_[frame_id];
        page_id_t victim_page_id = victim_page->GetPageId();
        spdlog::debug("NewPage: Evicted page {} from frame {}", victim_page_id, frame_id);
        if (victim_page->IsDirty()) {
            disk_manager_->WritePage(victim_page_id, victim_page->GetData());
            victim_page->SetDirty(false);
            spdlog::debug("NewPage: Flushed dirty page {} to disk on eviction", victim_page_id);
        }
        page_table_.erase(victim_page_id);
    }
    
    // 2. Allocate page ID from DiskManager
    *page_id = disk_manager_->AllocatePage();
    spdlog::debug("NewPage: Allocated new page_id {} from disk manager", *page_id);
    
    // 3. Initialize new page in selected frame
    Page *new_page = &pages_[frame_id];
    new_page->ResetMemory();
    new_page->page_id_ = *page_id;
    new_page->pin_count_ = 1;
    
    page_table_[*page_id] = frame_id;
    replacer_->Pin(frame_id);
    
    return new_page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    LockGuard lock(latch_);
    
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        // Page is not in the buffer pool, deallocate directly from disk
        disk_manager_->DeallocatePage(page_id);
        spdlog::debug("DeletePage: Page {} deallocated directly from disk", page_id);
        return true;
    }
    
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    
    if (page->GetPinCount() > 0) {
        spdlog::warn("DeletePage: Failed to delete page {} because it is pinned", page_id);
        return false;
    }
    
    // Remove from replacer and page table
    replacer_->Pin(frame_id);
    page_table_.erase(page_id);
    page->ResetMemory();
    free_list_.push_back(frame_id);
    
    disk_manager_->DeallocatePage(page_id);
    spdlog::debug("DeletePage: Page {} deleted from buffer pool frame {} and disk", page_id, frame_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    LockGuard lock(latch_);
    spdlog::info("FlushAllPages: Flushing all dirty pages to disk");
    for (auto &pair : page_table_) {
        frame_id_t frame_id = pair.second;
        Page *page = &pages_[frame_id];
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
            page->SetDirty(false);
            spdlog::debug("FlushAllPages: Flushed page {}", page->GetPageId());
        }
    }
}

int BufferPoolManager::GetPinCount(page_id_t page_id) {
    LockGuard lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return -1;
    }
    return pages_[iter->second].GetPinCount();
}

bool BufferPoolManager::IsCached(page_id_t page_id) {
    LockGuard lock(latch_);
    return page_table_.find(page_id) != page_table_.end();
}

} // namespace aether
