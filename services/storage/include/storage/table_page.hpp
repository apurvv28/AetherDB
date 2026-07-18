#pragma once

#include "storage/tuple.hpp"
#include "common/types.hpp"
#include <cstring>

namespace aether {

class TablePage {
public:
    struct Slot {
        uint32_t offset;
        uint32_t length;
    };

    void Init(page_id_t page_id, page_id_t prev_page_id = INVALID_PAGE_ID) {
        page_id_ = page_id;
        num_slots_ = 0;
        free_space_pointer_ = PAGE_SIZE;
        prev_page_id_ = prev_page_id;
        next_page_id_ = INVALID_PAGE_ID;
    }

    page_id_t GetPageId() const { return page_id_; }
    uint32_t GetNumSlots() const { return num_slots_; }
    uint32_t GetFreeSpacePointer() const { return free_space_pointer_; }
    page_id_t GetPrevPageId() const { return prev_page_id_; }
    page_id_t GetNextPageId() const { return next_page_id_; }

    void SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }
    void SetPrevPageId(page_id_t prev_page_id) { prev_page_id_ = prev_page_id; }

    bool InsertTuple(const Tuple &tuple, const Schema &schema, uint32_t *slot_num) {
        uint32_t tuple_len = tuple.GetSerializedLength();
        
        int target_slot = -1;
        for (uint32_t i = 0; i < num_slots_; ++i) {
            if (slots_[i].length == 0) {
                target_slot = i;
                break;
            }
        }

        uint32_t needed_space = tuple_len;
        if (target_slot == -1) {
            needed_space += sizeof(Slot);
        }

        uint32_t current_free_boundary = 20 + num_slots_ * sizeof(Slot);
        if (free_space_pointer_ < current_free_boundary || free_space_pointer_ - current_free_boundary < needed_space) {
            return false;
        }

        free_space_pointer_ -= tuple_len;
        tuple.Serialize(GetData() + free_space_pointer_);

        if (target_slot != -1) {
            slots_[target_slot].offset = free_space_pointer_;
            slots_[target_slot].length = tuple_len;
            *slot_num = target_slot;
        } else {
            slots_[num_slots_].offset = free_space_pointer_;
            slots_[num_slots_].length = tuple_len;
            *slot_num = num_slots_;
            num_slots_++;
        }

        return true;
    }

    bool GetTuple(uint32_t slot_num, Tuple *tuple, const Schema &schema) const {
        if (slot_num >= num_slots_) {
            return false;
        }
        if (slots_[slot_num].length == 0) {
            return false;
        }
        tuple->Deserialize(GetData() + slots_[slot_num].offset, schema);
        return true;
    }

    bool DeleteTuple(uint32_t slot_num) {
        if (slot_num >= num_slots_) {
            return false;
        }
        if (slots_[slot_num].length == 0) {
            return false;
        }
        slots_[slot_num].offset = 0;
        slots_[slot_num].length = 0;
        return true;
    }

    bool UpdateTuple(uint32_t slot_num, const Tuple &new_tuple, const Schema &schema) {
        if (slot_num >= num_slots_) {
            return false;
        }
        if (slots_[slot_num].length == 0) {
            return false;
        }

        uint32_t new_len = new_tuple.GetSerializedLength();
        if (new_len <= slots_[slot_num].length) {
            new_tuple.Serialize(GetData() + slots_[slot_num].offset);
            slots_[slot_num].length = new_len;
            return true;
        }

        uint32_t current_free_boundary = 20 + num_slots_ * sizeof(Slot);
        if (free_space_pointer_ < current_free_boundary || free_space_pointer_ - current_free_boundary < new_len) {
            return false;
        }

        free_space_pointer_ -= new_len;
        new_tuple.Serialize(GetData() + free_space_pointer_);
        slots_[slot_num].offset = free_space_pointer_;
        slots_[slot_num].length = new_len;
        return true;
    }

private:
    char *GetData() { return reinterpret_cast<char*>(this); }
    const char *GetData() const { return reinterpret_cast<const char*>(this); }

    page_id_t page_id_;
    uint32_t num_slots_;
    uint32_t free_space_pointer_;
    page_id_t prev_page_id_;
    page_id_t next_page_id_;
    Slot slots_[500];
};

} // namespace aether
