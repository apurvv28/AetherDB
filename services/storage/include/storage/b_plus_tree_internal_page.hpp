#pragma once

#include "storage/b_plus_tree_page.hpp"
#include "common/types.hpp"

namespace aether {

class BPlusTreeInternalPage : public BPlusTreePage {
public:
    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = 338) {
        SetPageType(BPlusTreePageType::INTERNAL_PAGE);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
        SetPageId(page_id);
    }

    int64_t KeyAt(int index) const { return keys_[index]; }
    void SetKeyAt(int index, int64_t key) { keys_[index] = key; }

    page_id_t ValueAt(int index) const { return values_[index]; }
    void SetValueAt(int index, page_id_t value) { values_[index] = value; }

    page_id_t Lookup(int64_t key) const {
        int low = 1, high = GetSize() - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (keys_[mid] <= key) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return values_[low - 1];
    }

    void Populate(page_id_t val0, int64_t key1, page_id_t val1) {
        SetValueAt(0, val0);
        SetKeyAt(1, key1);
        SetValueAt(1, val1);
        SetSize(2);
    }

    void InsertNodeAfter(page_id_t old_value, int64_t new_key, page_id_t new_value) {
        int index = -1;
        for (int i = 0; i < GetSize(); ++i) {
            if (values_[i] == old_value) {
                index = i;
                break;
            }
        }
        for (int i = GetSize(); i > index + 1; --i) {
            keys_[i] = keys_[i - 1];
            values_[i] = values_[i - 1];
        }
        keys_[index + 1] = new_key;
        values_[index + 1] = new_value;
        IncreaseSize(1);
    }

    void MoveHalfTo(BPlusTreeInternalPage *recipient) {
        int move_count = GetSize() / 2;
        int start_index = GetSize() - move_count;
        for (int i = start_index, j = 0; i < GetSize(); ++i, ++j) {
            recipient->keys_[j] = keys_[i];
            recipient->values_[j] = values_[i];
        }
        recipient->SetSize(move_count);
        SetSize(start_index);
    }

    void Remove(int index) {
        for (int i = index; i < GetSize() - 1; ++i) {
            keys_[i] = keys_[i + 1];
            values_[i] = values_[i + 1];
        }
        IncreaseSize(-1);
    }

    void MoveAllTo(BPlusTreeInternalPage *recipient, int64_t parent_key) {
        int dest_index = recipient->GetSize();
        recipient->keys_[dest_index] = parent_key;
        recipient->values_[dest_index] = values_[0];
        for (int i = 1; i < GetSize(); ++i) {
            recipient->keys_[dest_index + i] = keys_[i];
            recipient->values_[dest_index + i] = values_[i];
        }
        recipient->IncreaseSize(GetSize());
        SetSize(0);
    }

    void MoveFirstToEndOf(BPlusTreeInternalPage *recipient, int64_t parent_key) {
        int dest_idx = recipient->GetSize();
        recipient->keys_[dest_idx] = parent_key;
        recipient->values_[dest_idx] = values_[0];
        recipient->IncreaseSize(1);
        
        for (int i = 0; i < GetSize() - 1; ++i) {
            keys_[i] = keys_[i + 1];
            values_[i] = values_[i + 1];
        }
        IncreaseSize(-1);
    }

    void MoveLastToFrontOf(BPlusTreeInternalPage *recipient, int64_t parent_key) {
        for (int i = recipient->GetSize(); i > 0; --i) {
            recipient->keys_[i] = recipient->keys_[i - 1];
            recipient->values_[i] = recipient->values_[i - 1];
        }
        recipient->keys_[1] = parent_key;
        recipient->values_[0] = values_[GetSize() - 1];
        recipient->IncreaseSize(1);
        
        IncreaseSize(-1);
    }

private:
    int64_t keys_[339];
    page_id_t values_[339];
};

} // namespace aether
