#pragma once

#include "common/types.hpp"

namespace aether {

enum class BPlusTreePageType { LEAF_PAGE = 0, INTERNAL_PAGE = 1 };

class BPlusTreePage {
public:
    BPlusTreePageType GetPageType() const { return page_type_; }
    void SetPageType(BPlusTreePageType type) { page_type_ = type; }

    int GetSize() const { return size_; }
    void SetSize(int size) { size_ = size; }
    void IncreaseSize(int amount) { size_ += amount; }

    int GetMaxSize() const { return max_size_; }
    void SetMaxSize(int max_size) { max_size_ = max_size; }

    page_id_t GetParentPageId() const { return parent_page_id_; }
    void SetParentPageId(page_id_t parent_page_id) { parent_page_id_ = parent_page_id; }

    page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    bool IsLeafPage() const { return page_type_ == BPlusTreePageType::LEAF_PAGE; }
    bool IsRootPage() const { return parent_page_id_ == INVALID_PAGE_ID; }

private:
    BPlusTreePageType page_type_;
    int size_;
    int max_size_;
    page_id_t parent_page_id_;
    page_id_t page_id_;
};

} // namespace aether
