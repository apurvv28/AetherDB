# AetherDB Documentation: Phase 3 — B+ Tree Index

This document provides a detailed explanation of the design, node memory layouts, splitting and merging/redistribution algorithms, and the iterator structure behind AetherDB's disk-backed `BPlusTree` index.

---

## 1. Node Structures & Memory Layouts

Every node in the B+ Tree occupies exactly one 4KB physical page. Since the page content is cast from `char*` to class layouts in memory, the structures are standard-layout with no virtual functions, and fields are meticulously aligned.

```
       +-----------------------------------------------------------+
       | B+ Tree Page Header (aether::BPlusTreePage)               |
       +-----------------------------------------------------------+
       | page_type_ (BPlusTreePageType enum: 4 bytes)              |
       | size_ (int: 4 bytes)                                      |
       | max_size_ (int: 4 bytes)                                  |
       | parent_page_id_ (page_id_t: 4 bytes)                      |
       | page_id_ (page_id_t: 4 bytes)                             |
       +-----------------------------------------------------------+
```

### A. Leaf Node: `BPlusTreeLeafPage`
Leaf nodes store actual key-value mappings. For secondary indexes, values are `RID` records.
- **Header Extension**: `next_page_id_` (4 bytes) pointing to the right sibling leaf for O(1) sequential scans.
- **Keys Array**: `keys_` array of size `254` (`int64_t` keys).
- **Values Array**: `values_` array of size `254` (`RID` structures).
- **Memory Math**:
  `20 bytes (Header) + 4 bytes (next_page_id) + 254 * 8 bytes (keys) + 254 * 8 bytes (RIDs) = 4088 bytes <= 4096 bytes (PAGE_SIZE)`.
- **Threshold**: The default maximum leaf capacity `max_size` is set to `253`. During inserts, the node can temporarily grow to `254` elements before triggering a split, avoiding out-of-bounds corruption.

### B. Internal Node: `BPlusTreeInternalPage`
Internal nodes route traversals and do not store user values.
- **Routing Keys Array**: `keys_` array of size `339` (`int64_t` keys).
  - Note: `keys_[0]` is ignored/unused as `values_[0]` points to the subtree for keys smaller than `keys_[1]`.
- **Pointers Array**: `values_` array of size `339` (`page_id_t` pointing to child nodes).
- **Memory Math**:
  `20 bytes (Header) + 4 bytes (compiler padding) + 339 * 8 bytes (keys) + 339 * 4 bytes (page IDs) = 4088 bytes <= 4096 bytes (PAGE_SIZE)`.
- **Threshold**: The default maximum internal capacity `max_size` is set to `338`. It splits when the size reaches `339`.

---

## 2. Core Index Operations

### A. Point Lookup (`GetValue`)
1. Start at `root_page_id_`.
2. Fetch the active node via the `BufferPoolManager`.
3. If it is an internal page:
   - Perform binary search on the routing keys to find the first index `i` where `keys_[i] > key`.
   - Traverse to child page `values_[i-1]`.
   - Unpin the current page and fetch the child page. Repeat until a leaf page is reached.
4. If it is a leaf page:
   - Perform binary search on `keys_` for the exact key match.
   - If found, extract the `RID` value and return success. Otherwise, return failure.
   - Unpin the leaf page.

### B. Insertion & Overflow Splits (`Insert`)
1. **Empty Tree**: If `root_page_id_ == INVALID_PAGE_ID`, allocate a new root leaf page, insert the key-value pair, mark it dirty, and return.
2. **Find Leaf**: Traverse to the correct leaf page.
3. **Insert**: Insert the key-value pair in sorted order. If the key already exists, abort.
4. **Split Detection**: If the leaf page size exceeds `max_size` (size reaches `254`):
   - Allocate a new leaf page.
   - Move the second half of elements (`127` pairs) to the new leaf.
   - Update sibling pointers: `new_leaf->SetNextPageId(leaf->GetNextPageId())` and `leaf->SetNextPageId(new_leaf_id)`.
   - Propagate the split up: take the smallest key of the new leaf (`new_leaf->KeyAt(0)`) and insert it along with the new leaf's page ID into the parent internal page.
5. **Recursive Parent splits**:
   - If the parent node overflows, split it: allocate a new internal page, move half the routing keys and child pointers, and update the parent page pointer of all moved child pages on disk.
   - Push the boundary key (`new_parent->KeyAt(0)`) up to the grandparent recursively.
   - If the root overflows, create a new internal root page, making the two split nodes its children.

---

## 3. Deletions & Underflow Merging/Redistribution

### A. Deletion (`Delete`)
1. Locate the correct leaf page.
2. Search and remove the key. If the key doesn't exist, abort.
3. If the leaf is the root, delete the page when the size becomes 0.
4. **Underflow Detection**: If the node size falls below `max_size / 2`:
   - Retrieve the parent node.
   - Evaluate the left and right sibling nodes sharing the same parent.
   - Choose whether to **Redistribute** or **Merge**.

```
                +----------------------------+
                |           Parent           |
                |  ...  | separator |  ...   |
                +-----------|------|---------+
                            /      \
            +--------------+        +--------------+
            | Left Sibling |        | Right Sibling|
            |   (Node A)   |        |   (Node B)   |
            +--------------+        +--------------+
```

### B. Redistribution (Borrowing)
If a sibling has extra capacity (size $> \text{min\_size}$), we borrow one element to balance the tree and avoid merging overhead:
- **Leaf Borrowing**:
  - *From Left*: Move the last key-value pair of the left sibling to the front of the underflowed node. Update the parent routing key to the new first key of the underflowed node.
  - *From Right*: Move the first key-value pair of the right sibling to the end of the underflowed node. Update the parent routing key to the new first key of the right sibling.
- **Internal Borrowing**:
  - *From Left*: Pull down the parent routing key into the front of the underflowed node, move the last child pointer of the left sibling to the front of the underflowed node, and push up the left sibling's last key to the parent. Update the parent pointer of the moved child node.
  - *From Right*: Pull down the parent routing key into the end of the underflowed node, move the first child pointer of the right sibling to the end of the underflowed node, and push up the right sibling's first key to the parent. Update the parent pointer of the moved child node.

### C. Merging (Coalescing)
If siblings do not have extra capacity, we merge the underflowed node with a sibling:
- **Leaf Merging**:
  - Move all elements from the right leaf to the left leaf.
  - Update sibling linkage.
  - Remove the routing entry of the right leaf from the parent.
- **Internal Merging**:
  - Pull down the separating parent key into the left node.
  - Move all keys and child pointers of the right node into the left node.
  - Update parent pointers of all child pages moved to the left node.
  - Remove the routing entry of the right node from the parent.
- **Recursion**: If the parent underflows after removal, recursively trigger `HandleUnderflow` on the parent node up to the root. If the root internal node size falls to 1, its single child becomes the new root, and the old root is deleted.

---

## 4. Range Iteration & RAII Pin Protection

AetherDB provides a sequential range iterator `BPlusTreeIterator`:
- **Acquiring**: Calling `Begin(key)` finds the leaf page containing the key, finds the key index within the page, and constructs the iterator.
- **Traversal**: Incrementing (`operator++`) increments the index. When it reaches the end of the page, it uses `leaf->GetNextPageId()` to fetch the next sibling leaf page from the buffer pool, unpinning the previous page.
- **RAII Pins**: To prevent memory leaks and buffer pool pollution:
  - The iterator destructor unpins the active page.
  - Copy constructors and copy assignments invoke `FetchPage` to increment page pin counts correctly, matching the resource state.
  - Move constructors and move assignments safely transfer page pin ownership.
