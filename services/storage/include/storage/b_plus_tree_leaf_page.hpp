#pragma once

#include "storage/b_plus_tree_page.hpp"
#include "common/types.hpp"

namespace aether {

class BPlusTreeLeafPage : public BPlusTreePage {
public:
    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = 253) {
        SetPageType(BPlusTreePageType::LEAF_PAGE);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
        SetPageId(page_id);
        SetNextPageId(INVALID_PAGE_ID);
    }

    page_id_t GetNextPageId() const { return next_page_id_; }
    void SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

    int KeyIndex(int64_t key) const {
        int low = 0, high = GetSize() - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (keys_[mid] < key) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return low;
    }

    int64_t KeyAt(int index) const { return keys_[index]; }
    RID ValueAt(int index) const { return values_[index]; }

    bool Lookup(int64_t key, RID *result) const {
        int index = KeyIndex(key);
        if (index < GetSize() && keys_[index] == key) {
            *result = values_[index];
            return true;
        }
        return false;
    }

    int Insert(int64_t key, const RID &value) {
        int index = KeyIndex(key);
        if (index < GetSize() && keys_[index] == key) {
            return GetSize();
        }
        for (int i = GetSize(); i > index; --i) {
            keys_[i] = keys_[i - 1];
            values_[i] = values_[i - 1];
        }
        keys_[index] = key;
        values_[index] = value;
        IncreaseSize(1);
        return GetSize();
    }

    void MoveHalfTo(BPlusTreeLeafPage *recipient) {
        int move_count = GetSize() / 2;
        int start_index = GetSize() - move_count;
        for (int i = start_index, j = 0; i < GetSize(); ++i, ++j) {
            recipient->keys_[j] = keys_[i];
            recipient->values_[j] = values_[i];
        }
        recipient->SetSize(move_count);
        SetSize(start_index);
    }

    void RemoveAt(int index) {
        for (int i = index; i < GetSize() - 1; ++i) {
            keys_[i] = keys_[i + 1];
            values_[i] = values_[i + 1];
        }
        IncreaseSize(-1);
    }

    bool Remove(int64_t key) {
        int index = KeyIndex(key);
        if (index < GetSize() && keys_[index] == key) {
            RemoveAt(index);
            return true;
        }
        return false;
    }

    void MoveAllTo(BPlusTreeLeafPage *recipient) {
        int dest_index = recipient->GetSize();
        for (int i = 0; i < GetSize(); ++i) {
            recipient->keys_[dest_index + i] = keys_[i];
            recipient->values_[dest_index + i] = values_[i];
        }
        recipient->IncreaseSize(GetSize());
        recipient->SetNextPageId(GetNextPageId());
        SetSize(0);
    }

    void MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
        int64_t first_key = keys_[0];
        RID first_val = values_[0];
        recipient->Insert(first_key, first_val);
        RemoveAt(0);
    }

    void MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
        int last_idx = GetSize() - 1;
        int64_t last_key = keys_[last_idx];
        RID last_val = values_[last_idx];
        recipient->Insert(last_key, last_val);
        RemoveAt(last_idx);
    }

private:
    page_id_t next_page_id_;
    int64_t keys_[254];
    RID values_[254];
};

} // namespace aether
