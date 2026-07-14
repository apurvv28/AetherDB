#pragma once

#include "common/types.hpp"
#include <cstring>

namespace aether {

class Page {
public:
    Page() { ResetMemory(); }
    ~Page() = default;

    // Get the page data buffer
    char *GetData() { return data_; }
    const char *GetData() const { return data_; }

    // Get the page ID
    page_id_t GetPageId() const { return page_id_; }

    // Get the pin count
    int GetPinCount() const { return pin_count_; }

    // Check if the page is dirty (modified in memory but not persisted to disk)
    bool IsDirty() const { return is_dirty_; }

    // Set the dirty flag
    void SetDirty(bool is_dirty) { is_dirty_ = is_dirty; }

    // Reset the page frame's memory and metadata
    void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
    }

private:
    friend class BufferPoolManager;

    char data_[PAGE_SIZE];
    page_id_t page_id_{INVALID_PAGE_ID};
    int pin_count_{0};
    bool is_dirty_{false};
};

} // namespace aether
