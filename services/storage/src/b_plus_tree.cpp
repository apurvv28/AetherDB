#include "storage/b_plus_tree.hpp"
#include "common/logger.hpp"

namespace aether {

BPlusTree::BPlusTree(page_id_t root_page_id, BufferPoolManager *bpm)
    : root_page_id_(root_page_id), bpm_(bpm) {}

bool BPlusTree::GetValue(int64_t key, RID *result) {
    if (IsEmpty()) {
        return false;
    }
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }
    auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    bool status = leaf->Lookup(key, result);
    bpm_->UnpinPage(leaf_page->GetPageId(), false);
    return status;
}

Page *BPlusTree::FindLeafPage(int64_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        return nullptr;
    }
    page_id_t curr_id = root_page_id_;
    Page *curr_page = bpm_->FetchPage(curr_id);
    
    while (true) {
        auto *btree_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
        if (btree_page->IsLeafPage()) {
            return curr_page;
        }
        
        auto *internal_page = reinterpret_cast<BPlusTreeInternalPage*>(curr_page->GetData());
        page_id_t next_id = internal_page->Lookup(key);
        
        bpm_->UnpinPage(curr_id, false);
        curr_id = next_id;
        curr_page = bpm_->FetchPage(curr_id);
    }
}

bool BPlusTree::Insert(int64_t key, const RID &value) {
    if (IsEmpty()) {
        page_id_t new_root_id;
        Page *root_page = bpm_->NewPage(&new_root_id);
        if (root_page == nullptr) {
            return false;
        }
        auto *root = reinterpret_cast<BPlusTreeLeafPage*>(root_page->GetData());
        root->Init(new_root_id, INVALID_PAGE_ID);
        root->Insert(key, value);
        root_page_id_ = new_root_id;
        bpm_->UnpinPage(new_root_id, true);
        return true;
    }

    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }

    auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    int old_size = leaf->GetSize();
    int new_size = leaf->Insert(key, value);
    if (new_size == old_size) {
        // Key already existed.
        bpm_->UnpinPage(leaf_page->GetPageId(), false);
        return false;
    }

    if (new_size <= leaf->GetMaxSize()) {
        bpm_->UnpinPage(leaf_page->GetPageId(), true);
        return true;
    }

    // Leaf page overflow. Split!
    page_id_t new_leaf_id;
    Page *new_leaf_page = bpm_->NewPage(&new_leaf_id);
    if (new_leaf_page == nullptr) {
        bpm_->UnpinPage(leaf_page->GetPageId(), true);
        return false;
    }

    auto *new_leaf = reinterpret_cast<BPlusTreeLeafPage*>(new_leaf_page->GetData());
    new_leaf->Init(new_leaf_id, leaf->GetParentPageId());
    
    new_leaf->SetNextPageId(leaf->GetNextPageId());
    leaf->SetNextPageId(new_leaf_id);

    leaf->MoveHalfTo(new_leaf);

    int64_t split_key = new_leaf->KeyAt(0);
    InsertIntoParent(leaf, split_key, new_leaf);

    bpm_->UnpinPage(leaf_page->GetPageId(), true);
    bpm_->UnpinPage(new_leaf_page->GetPageId(), true);
    return true;
}

void BPlusTree::InsertIntoParent(BPlusTreePage *old_child, int64_t key, BPlusTreePage *new_child) {
    if (old_child->IsRootPage()) {
        page_id_t new_root_id;
        Page *new_root_page = bpm_->NewPage(&new_root_id);
        if (new_root_page == nullptr) {
            return;
        }
        auto *new_root = reinterpret_cast<BPlusTreeInternalPage*>(new_root_page->GetData());
        new_root->Init(new_root_id, INVALID_PAGE_ID);
        new_root->Populate(old_child->GetPageId(), key, new_child->GetPageId());
        
        old_child->SetParentPageId(new_root_id);
        new_child->SetParentPageId(new_root_id);
        
        root_page_id_ = new_root_id;
        bpm_->UnpinPage(new_root_id, true);
        return;
    }

    page_id_t parent_id = old_child->GetParentPageId();
    Page *parent_page = bpm_->FetchPage(parent_id);
    if (parent_page == nullptr) {
        return;
    }
    auto *parent = reinterpret_cast<BPlusTreeInternalPage*>(parent_page->GetData());
    
    parent->InsertNodeAfter(old_child->GetPageId(), key, new_child->GetPageId());
    new_child->SetParentPageId(parent_id);

    if (parent->GetSize() <= parent->GetMaxSize()) {
        bpm_->UnpinPage(parent_id, true);
        return;
    }

    // Parent overflow. Split!
    page_id_t new_parent_id;
    Page *new_parent_page = bpm_->NewPage(&new_parent_id);
    if (new_parent_page == nullptr) {
        bpm_->UnpinPage(parent_id, true);
        return;
    }
    
    auto *new_parent = reinterpret_cast<BPlusTreeInternalPage*>(new_parent_page->GetData());
    new_parent->Init(new_parent_id, parent->GetParentPageId());
    
    parent->MoveHalfTo(new_parent);

    // Update parent pointers of moved children
    for (int i = 0; i < new_parent->GetSize(); ++i) {
        page_id_t child_id = new_parent->ValueAt(i);
        Page *c_page = bpm_->FetchPage(child_id);
        if (c_page != nullptr) {
            auto *c = reinterpret_cast<BPlusTreePage*>(c_page->GetData());
            c->SetParentPageId(new_parent_id);
            bpm_->UnpinPage(child_id, true);
        }
    }

    int64_t push_up_key = new_parent->KeyAt(0);

    InsertIntoParent(parent, push_up_key, new_parent);

    bpm_->UnpinPage(parent_id, true);
    bpm_->UnpinPage(new_parent_id, true);
}

bool BPlusTree::Delete(int64_t key) {
    if (IsEmpty()) {
        return false;
    }
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }
    auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    bool deleted = leaf->Remove(key);
    if (!deleted) {
        bpm_->UnpinPage(leaf_page->GetPageId(), false);
        return false;
    }

    if (leaf->IsRootPage()) {
        if (leaf->GetSize() == 0) {
            bpm_->DeletePage(root_page_id_);
            root_page_id_ = INVALID_PAGE_ID;
        } else {
            bpm_->UnpinPage(leaf_page->GetPageId(), true);
        }
        return true;
    }

    int min_size = leaf->GetMaxSize() / 2;
    if (leaf->GetSize() >= min_size) {
        bpm_->UnpinPage(leaf_page->GetPageId(), true);
        return true;
    }

    HandleUnderflow(leaf);
    bpm_->UnpinPage(leaf_page->GetPageId(), true);
    return true;
}

void BPlusTree::HandleUnderflow(BPlusTreePage *node) {
    if (node->IsRootPage()) {
        if (!node->IsLeafPage() && node->GetSize() == 1) {
            auto *internal = reinterpret_cast<BPlusTreeInternalPage*>(node);
            page_id_t new_root_id = internal->ValueAt(0);
            
            Page *new_root_page = bpm_->FetchPage(new_root_id);
            if (new_root_page != nullptr) {
                auto *new_root = reinterpret_cast<BPlusTreePage*>(new_root_page->GetData());
                new_root->SetParentPageId(INVALID_PAGE_ID);
                bpm_->UnpinPage(new_root_id, true);
            }
            
            bpm_->DeletePage(root_page_id_);
            root_page_id_ = new_root_id;
        }
        return;
    }

    page_id_t parent_id = node->GetParentPageId();
    Page *parent_page = bpm_->FetchPage(parent_id);
    if (parent_page == nullptr) {
        return;
    }
    auto *parent = reinterpret_cast<BPlusTreeInternalPage*>(parent_page->GetData());
    
    int child_idx = -1;
    for (int i = 0; i < parent->GetSize(); ++i) {
        if (parent->ValueAt(i) == node->GetPageId()) {
            child_idx = i;
            break;
        }
    }

    if (child_idx > 0) {
        page_id_t left_id = parent->ValueAt(child_idx - 1);
        Page *left_page = bpm_->FetchPage(left_id);
        if (left_page != nullptr) {
            auto *left = reinterpret_cast<BPlusTreePage*>(left_page->GetData());
            int min_size = left->GetMaxSize() / 2;
            if (left->GetSize() > min_size) {
                Redistribute(node, left, parent, child_idx, true);
                bpm_->UnpinPage(left_id, true);
                bpm_->UnpinPage(parent_id, true);
                return;
            }
            bpm_->UnpinPage(left_id, false);
        }
    }

    if (child_idx < parent->GetSize() - 1) {
        page_id_t right_id = parent->ValueAt(child_idx + 1);
        Page *right_page = bpm_->FetchPage(right_id);
        if (right_page != nullptr) {
            auto *right = reinterpret_cast<BPlusTreePage*>(right_page->GetData());
            int min_size = right->GetMaxSize() / 2;
            if (right->GetSize() > min_size) {
                Redistribute(node, right, parent, child_idx, false);
                bpm_->UnpinPage(right_id, true);
                bpm_->UnpinPage(parent_id, true);
                return;
            }
            bpm_->UnpinPage(right_id, false);
        }
    }

    if (child_idx > 0) {
        page_id_t left_id = parent->ValueAt(child_idx - 1);
        Page *left_page = bpm_->FetchPage(left_id);
        if (left_page != nullptr) {
            auto *left = reinterpret_cast<BPlusTreePage*>(left_page->GetData());
            Merge(left, node, parent, child_idx);
            bpm_->UnpinPage(left_id, true);
            bpm_->DeletePage(node->GetPageId());
        }
    } else {
        page_id_t right_id = parent->ValueAt(child_idx + 1);
        Page *right_page = bpm_->FetchPage(right_id);
        if (right_page != nullptr) {
            auto *right = reinterpret_cast<BPlusTreePage*>(right_page->GetData());
            Merge(node, right, parent, child_idx + 1);
            bpm_->UnpinPage(right_id, true);
            bpm_->DeletePage(right_id);
        }
    }

    int parent_min_size = parent->GetMaxSize() / 2;
    if (parent->GetSize() < parent_min_size) {
        HandleUnderflow(parent);
    }
    
    bpm_->UnpinPage(parent_id, true);
}

bool BPlusTree::Redistribute(BPlusTreePage *node, BPlusTreePage *sibling, BPlusTreeInternalPage *parent, int child_idx, bool from_left) {
    if (node->IsLeafPage()) {
        auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(node);
        auto *sib = reinterpret_cast<BPlusTreeLeafPage*>(sibling);
        
        if (from_left) {
            sib->MoveLastToFrontOf(leaf);
            parent->SetKeyAt(child_idx, leaf->KeyAt(0));
        } else {
            sib->MoveFirstToEndOf(leaf);
            parent->SetKeyAt(child_idx + 1, sib->KeyAt(0));
        }
    } else {
        auto *internal = reinterpret_cast<BPlusTreeInternalPage*>(node);
        auto *sib = reinterpret_cast<BPlusTreeInternalPage*>(sibling);
        
        if (from_left) {
            int64_t parent_key = parent->KeyAt(child_idx);
            int64_t new_parent_key = sib->KeyAt(sib->GetSize() - 1);
            sib->MoveLastToFrontOf(internal, parent_key);
            parent->SetKeyAt(child_idx, new_parent_key);
            
            page_id_t moved_child_id = internal->ValueAt(0);
            Page *c_page = bpm_->FetchPage(moved_child_id);
            if (c_page != nullptr) {
                reinterpret_cast<BPlusTreePage*>(c_page->GetData())->SetParentPageId(internal->GetPageId());
                bpm_->UnpinPage(moved_child_id, true);
            }
        } else {
            int64_t parent_key = parent->KeyAt(child_idx + 1);
            int64_t new_parent_key = sib->KeyAt(1);
            sib->MoveFirstToEndOf(internal, parent_key);
            parent->SetKeyAt(child_idx + 1, new_parent_key);
            
            page_id_t moved_child_id = internal->ValueAt(internal->GetSize() - 1);
            Page *c_page = bpm_->FetchPage(moved_child_id);
            if (c_page != nullptr) {
                reinterpret_cast<BPlusTreePage*>(c_page->GetData())->SetParentPageId(internal->GetPageId());
                bpm_->UnpinPage(moved_child_id, true);
            }
        }
    }
    return true;
}

void BPlusTree::Merge(BPlusTreePage *node, BPlusTreePage *sibling, BPlusTreeInternalPage *parent, int child_idx) {
    if (node->IsLeafPage()) {
        auto *left = reinterpret_cast<BPlusTreeLeafPage*>(node);
        auto *right = reinterpret_cast<BPlusTreeLeafPage*>(sibling);
        
        right->MoveAllTo(left);
        
        parent->Remove(child_idx);
    } else {
        auto *left = reinterpret_cast<BPlusTreeInternalPage*>(node);
        auto *right = reinterpret_cast<BPlusTreeInternalPage*>(sibling);
        
        int64_t parent_key = parent->KeyAt(child_idx);
        
        right->MoveAllTo(left, parent_key);
        
        for (int i = 0; i < left->GetSize(); ++i) {
            page_id_t child_id = left->ValueAt(i);
            Page *c_page = bpm_->FetchPage(child_id);
            if (c_page != nullptr) {
                reinterpret_cast<BPlusTreePage*>(c_page->GetData())->SetParentPageId(left->GetPageId());
                bpm_->UnpinPage(child_id, true);
            }
        }
        
        parent->Remove(child_idx);
    }
}

BPlusTreeIterator BPlusTree::Begin() {
    if (IsEmpty()) {
        return End();
    }
    page_id_t curr_id = root_page_id_;
    Page *curr_page = bpm_->FetchPage(curr_id);
    while (true) {
        auto *btree_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
        if (btree_page->IsLeafPage()) {
            bpm_->UnpinPage(curr_id, false);
            return BPlusTreeIterator(bpm_, curr_id, 0);
        }
        auto *internal = reinterpret_cast<BPlusTreeInternalPage*>(curr_page->GetData());
        page_id_t leftmost_child = internal->ValueAt(0);
        bpm_->UnpinPage(curr_id, false);
        curr_id = leftmost_child;
        curr_page = bpm_->FetchPage(curr_id);
    }
}

BPlusTreeIterator BPlusTree::Begin(int64_t key) {
    if (IsEmpty()) {
        return End();
    }
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return End();
    }
    auto *leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    int idx = leaf->KeyIndex(key);
    page_id_t leaf_id = leaf_page->GetPageId();
    bpm_->UnpinPage(leaf_id, false);
    return BPlusTreeIterator(bpm_, leaf_id, idx);
}

BPlusTreeIterator BPlusTree::End() {
    return BPlusTreeIterator(bpm_, INVALID_PAGE_ID, 0);
}

} // namespace aether
