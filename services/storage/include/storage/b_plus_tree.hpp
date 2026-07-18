#pragma once

#include "storage/buffer_pool_manager.hpp"
#include "storage/b_plus_tree_page.hpp"
#include "storage/b_plus_tree_leaf_page.hpp"
#include "storage/b_plus_tree_internal_page.hpp"
#include <utility>

namespace aether {

class BPlusTreeIterator {
public:
    BPlusTreeIterator(BufferPoolManager *bpm, page_id_t page_id, int index)
        : bpm_(bpm), curr_page_id_(page_id), curr_index_(index) {
        if (curr_page_id_ != INVALID_PAGE_ID) {
            curr_page_ = bpm_->FetchPage(curr_page_id_);
        }
    }

    ~BPlusTreeIterator() {
        if (curr_page_id_ != INVALID_PAGE_ID && curr_page_ != nullptr) {
            bpm_->UnpinPage(curr_page_id_, false);
        }
    }

    // Copy constructor
    BPlusTreeIterator(const BPlusTreeIterator &other)
        : bpm_(other.bpm_), curr_page_id_(other.curr_page_id_), curr_index_(other.curr_index_) {
        if (curr_page_id_ != INVALID_PAGE_ID) {
            curr_page_ = bpm_->FetchPage(curr_page_id_);
        }
    }

    // Copy assignment
    BPlusTreeIterator& operator=(const BPlusTreeIterator &other) {
        if (this != &other) {
            if (curr_page_id_ != INVALID_PAGE_ID && curr_page_ != nullptr) {
                bpm_->UnpinPage(curr_page_id_, false);
            }
            bpm_ = other.bpm_;
            curr_page_id_ = other.curr_page_id_;
            curr_index_ = other.curr_index_;
            if (curr_page_id_ != INVALID_PAGE_ID) {
                curr_page_ = bpm_->FetchPage(curr_page_id_);
            } else {
                curr_page_ = nullptr;
            }
        }
        return *this;
    }

    // Move constructor
    BPlusTreeIterator(BPlusTreeIterator &&other) noexcept
        : bpm_(other.bpm_), curr_page_id_(other.curr_page_id_), curr_index_(other.curr_index_), curr_page_(other.curr_page_) {
        other.curr_page_id_ = INVALID_PAGE_ID;
        other.curr_page_ = nullptr;
    }

    // Move assignment
    BPlusTreeIterator& operator=(BPlusTreeIterator &&other) noexcept {
        if (this != &other) {
            if (curr_page_id_ != INVALID_PAGE_ID && curr_page_ != nullptr) {
                bpm_->UnpinPage(curr_page_id_, false);
            }
            bpm_ = other.bpm_;
            curr_page_id_ = other.curr_page_id_;
            curr_index_ = other.curr_index_;
            curr_page_ = other.curr_page_;
            other.curr_page_id_ = INVALID_PAGE_ID;
            other.curr_page_ = nullptr;
        }
        return *this;
    }

    bool IsEnd() const { return curr_page_id_ == INVALID_PAGE_ID; }

    std::pair<int64_t, RID> operator*() const {
        auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page_->GetData());
        return {leaf->KeyAt(curr_index_), leaf->ValueAt(curr_index_)};
    }

    BPlusTreeIterator& operator++() {
        if (curr_page_id_ == INVALID_PAGE_ID) {
            return *this;
        }
        auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(curr_page_->GetData());
        curr_index_++;
        if (curr_index_ >= leaf->GetSize()) {
            page_id_t next_id = leaf->GetNextPageId();
            bpm_->UnpinPage(curr_page_id_, false);
            curr_page_id_ = next_id;
            curr_index_ = 0;
            if (curr_page_id_ != INVALID_PAGE_ID) {
                curr_page_ = bpm_->FetchPage(curr_page_id_);
            } else {
                curr_page_ = nullptr;
            }
        }
        return *this;
    }

    bool operator==(const BPlusTreeIterator &other) const {
        return curr_page_id_ == other.curr_page_id_ && curr_index_ == other.curr_index_;
    }

    bool operator!=(const BPlusTreeIterator &other) const {
        return !(*this == other);
    }

private:
    BufferPoolManager *bpm_;
    page_id_t curr_page_id_;
    int curr_index_;
    Page *curr_page_{nullptr};
};

class BPlusTree {
public:
    explicit BPlusTree(page_id_t root_page_id, BufferPoolManager *bpm);
    ~BPlusTree() = default;

    page_id_t GetRootPageId() const { return root_page_id_; }

    bool IsEmpty() const { return root_page_id_ == INVALID_PAGE_ID; }

    bool GetValue(int64_t key, RID *result);

    bool Insert(int64_t key, const RID &value);

    bool Delete(int64_t key);

    BPlusTreeIterator Begin();
    BPlusTreeIterator Begin(int64_t key);
    BPlusTreeIterator End();

private:
    Page *FindLeafPage(int64_t key);
    
    void InsertIntoLeaf(BPlusTreeLeafPage *leaf, int64_t key, const RID &value);
    void InsertIntoParent(BPlusTreePage *old_child, int64_t key, BPlusTreePage *new_child);
    
    void HandleUnderflow(BPlusTreePage *node);
    bool Redistribute(BPlusTreePage *node, BPlusTreePage *sibling, BPlusTreeInternalPage *parent, int child_idx, bool from_left);
    void Merge(BPlusTreePage *node, BPlusTreePage *sibling, BPlusTreeInternalPage *parent, int child_idx);

    page_id_t root_page_id_;
    BufferPoolManager *bpm_;
};

} // namespace aether
