#pragma once

#include "storage/buffer_pool_manager.hpp"
#include "storage/table_page.hpp"
#include "common/types.hpp"

namespace aether {

class TableHeap {
public:
    TableHeap(BufferPoolManager *bpm, page_id_t first_page_id)
        : bpm_(bpm), first_page_id_(first_page_id) {}

    explicit TableHeap(BufferPoolManager *bpm) : bpm_(bpm) {
        Page *page = bpm_->NewPage(&first_page_id_);
        if (page != nullptr) {
            auto *table_page = reinterpret_cast<TablePage*>(page->GetData());
            table_page->Init(first_page_id_, INVALID_PAGE_ID);
            bpm_->UnpinPage(first_page_id_, true);
        }
    }

    page_id_t GetFirstPageId() const { return first_page_id_; }

    bool InsertTuple(const Tuple &tuple, const Schema &schema, RID *rid) {
        page_id_t curr_id = first_page_id_;
        while (curr_id != INVALID_PAGE_ID) {
            Page *page = bpm_->FetchPage(curr_id);
            if (page == nullptr) {
                return false;
            }
            auto *table_page = reinterpret_cast<TablePage*>(page->GetData());
            uint32_t slot_num;
            if (table_page->InsertTuple(tuple, schema, &slot_num)) {
                rid->page_id = curr_id;
                rid->slot_num = slot_num;
                bpm_->UnpinPage(curr_id, true);
                return true;
            }
            
            page_id_t next_id = table_page->GetNextPageId();
            if (next_id == INVALID_PAGE_ID) {
                page_id_t new_page_id;
                Page *new_page = bpm_->NewPage(&new_page_id);
                if (new_page == nullptr) {
                    bpm_->UnpinPage(curr_id, false);
                    return false;
                }
                auto *new_table_page = reinterpret_cast<TablePage*>(new_page->GetData());
                new_table_page->Init(new_page_id, curr_id);
                
                table_page->SetNextPageId(new_page_id);
                
                new_table_page->InsertTuple(tuple, schema, &slot_num);
                rid->page_id = new_page_id;
                rid->slot_num = slot_num;
                
                bpm_->UnpinPage(curr_id, true);
                bpm_->UnpinPage(new_page_id, true);
                return true;
            }
            
            bpm_->UnpinPage(curr_id, false);
            curr_id = next_id;
        }
        return false;
    }

    bool GetTuple(const RID &rid, Tuple *tuple, const Schema &schema) {
        Page *page = bpm_->FetchPage(rid.page_id);
        if (page == nullptr) {
            return false;
        }
        auto *table_page = reinterpret_cast<TablePage*>(page->GetData());
        bool status = table_page->GetTuple(rid.slot_num, tuple, schema);
        bpm_->UnpinPage(rid.page_id, false);
        return status;
    }

    bool UpdateTuple(const RID &rid, const Tuple &new_tuple, const Schema &schema) {
        Page *page = bpm_->FetchPage(rid.page_id);
        if (page == nullptr) {
            return false;
        }
        auto *table_page = reinterpret_cast<TablePage*>(page->GetData());
        bool status = table_page->UpdateTuple(rid.slot_num, new_tuple, schema);
        bpm_->UnpinPage(rid.page_id, status);
        return status;
    }

    bool DeleteTuple(const RID &rid) {
        Page *page = bpm_->FetchPage(rid.page_id);
        if (page == nullptr) {
            return false;
        }
        auto *table_page = reinterpret_cast<TablePage*>(page->GetData());
        bool status = table_page->DeleteTuple(rid.slot_num);
        bpm_->UnpinPage(rid.page_id, status);
        return status;
    }

private:
    BufferPoolManager *bpm_;
    page_id_t first_page_id_{INVALID_PAGE_ID};
};

} // namespace aether
