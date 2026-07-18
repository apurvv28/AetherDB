#include "storage/disk_manager.hpp"
#include "storage/replacer.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/b_plus_tree.hpp"
#include "common/logger.hpp"
#include "aether_test.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <random>
#include <algorithm>

class BPlusTreeTest : public ::testing::Test {
protected:
    const std::string db_filename = "test_b_plus_tree.db";

    void SetUp() override {
        aether::InitializeLogger();
        std::remove(db_filename.c_str());
    }

    void TearDown() override {
        std::remove(db_filename.c_str());
    }
};

TEST_F(BPlusTreeTest, BasicInsertionAndLookup) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(50);
    aether::BufferPoolManager bpm(50, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);

    // Insert a few key-value pairs
    EXPECT_TRUE(tree.Insert(10, {1, 100}));
    EXPECT_TRUE(tree.Insert(20, {2, 200}));
    EXPECT_TRUE(tree.Insert(5, {0, 50}));
    EXPECT_TRUE(tree.Insert(15, {1, 150}));

    // Reinserting same key should return false
    EXPECT_FALSE(tree.Insert(10, {1, 101}));

    // Verify lookups
    aether::RID res;
    EXPECT_TRUE(tree.GetValue(10, &res));
    EXPECT_EQ(res.page_id, 1);
    EXPECT_EQ(res.slot_num, 100);

    EXPECT_TRUE(tree.GetValue(20, &res));
    EXPECT_EQ(res.page_id, 2);
    EXPECT_EQ(res.slot_num, 200);

    EXPECT_TRUE(tree.GetValue(5, &res));
    EXPECT_EQ(res.page_id, 0);
    EXPECT_EQ(res.slot_num, 50);

    EXPECT_TRUE(tree.GetValue(15, &res));
    EXPECT_EQ(res.page_id, 1);
    EXPECT_EQ(res.slot_num, 150);

    // Lookup non-existent key
    EXPECT_FALSE(tree.GetValue(100, &res));
}

TEST_F(BPlusTreeTest, LeafSplitTest) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(50);
    aether::BufferPoolManager bpm(50, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);

    // Leaf capacity is 254. Let's insert 300 elements to force a leaf split.
    for (int i = 1; i <= 300; ++i) {
        aether::RID rid = {static_cast<aether::page_id_t>(i), static_cast<uint32_t>(i * 10)};
        EXPECT_TRUE(tree.Insert(i, rid));
    }

    // Verify they can all be read back
    for (int i = 1; i <= 300; ++i) {
        aether::RID res;
        EXPECT_TRUE(tree.GetValue(i, &res));
        EXPECT_EQ(res.page_id, i);
        EXPECT_EQ(res.slot_num, i * 10);
    }
}

TEST_F(BPlusTreeTest, InternalSplitTest) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    // Use a larger buffer pool to hold intermediate split pages
    aether::LRUReplacer replacer(200);
    aether::BufferPoolManager bpm(200, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);

    // Internal capacity is 339.
    // Let's insert 3000 elements to force multiple levels of internal splits.
    for (int i = 1; i <= 3000; ++i) {
        aether::RID rid = {static_cast<aether::page_id_t>(i), static_cast<uint32_t>(i * 5)};
        EXPECT_TRUE(tree.Insert(i, rid));
    }

    // Verify lookup of all elements
    for (int i = 1; i <= 3000; ++i) {
        aether::RID res;
        EXPECT_TRUE(tree.GetValue(i, &res));
        EXPECT_EQ(res.page_id, i);
        EXPECT_EQ(res.slot_num, i * 5);
    }
}

TEST_F(BPlusTreeTest, RangeIteratorTest) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(100);
    aether::BufferPoolManager bpm(100, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);

    // Insert out of order
    std::vector<int64_t> keys = {50, 10, 30, 40, 20, 70, 60};
    for (auto k : keys) {
        tree.Insert(k, {static_cast<aether::page_id_t>(k), 999});
    }

    // Sort to verify sequential access
    std::sort(keys.begin(), keys.end());

    // Iteration from Begin
    auto it = tree.Begin();
    auto end = tree.End();
    
    size_t idx = 0;
    while (it != end) {
        auto pair = *it;
        EXPECT_EQ(pair.first, keys[idx]);
        EXPECT_EQ(pair.second.page_id, keys[idx]);
        EXPECT_EQ(pair.second.slot_num, 999);
        ++it;
        idx++;
    }
    EXPECT_EQ(idx, keys.size());

    // Iteration from a specific key
    auto it_from = tree.Begin(30);
    EXPECT_EQ((*it_from).first, 30);
    ++it_from;
    EXPECT_EQ((*it_from).first, 40);
    ++it_from;
    EXPECT_EQ((*it_from).first, 50);
}

TEST_F(BPlusTreeTest, DeletionTest) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(100);
    aether::BufferPoolManager bpm(100, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);

    // Insert 10 keys
    for (int i = 1; i <= 10; ++i) {
        tree.Insert(i, {static_cast<aether::page_id_t>(i), 100});
    }

    // Delete a few
    EXPECT_TRUE(tree.Delete(5));
    EXPECT_TRUE(tree.Delete(1));
    EXPECT_TRUE(tree.Delete(10));

    // Try deleting non-existent key
    EXPECT_FALSE(tree.Delete(5));
    EXPECT_FALSE(tree.Delete(99));

    // Verify lookups
    aether::RID res;
    EXPECT_FALSE(tree.GetValue(5, &res));
    EXPECT_FALSE(tree.GetValue(1, &res));
    EXPECT_FALSE(tree.GetValue(10, &res));
    
    EXPECT_TRUE(tree.GetValue(2, &res));
    EXPECT_EQ(res.page_id, 2);
}

TEST_F(BPlusTreeTest, MapStressTest) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(200);
    aether::BufferPoolManager bpm(200, disk_manager.get(), &replacer);

    aether::BPlusTree tree(aether::INVALID_PAGE_ID, &bpm);
    std::map<int64_t, aether::RID> ref_map;

    std::mt19937 rng(42); // Seeded random generator
    std::vector<int64_t> keys;

    // Phase A: Random insertions (1500 elements)
    for (int i = 0; i < 1500; ++i) {
        int64_t key = std::uniform_int_distribution<int64_t>(1, 10000)(rng);
        aether::RID value = {static_cast<aether::page_id_t>(i), static_cast<uint32_t>(key * 2)};
        
        bool tree_inserted = tree.Insert(key, value);
        bool map_inserted = ref_map.insert({key, value}).second;
        EXPECT_EQ(tree_inserted, map_inserted);
        
        if (tree_inserted) {
            keys.push_back(key);
        }
    }

    // Phase B: Verify all lookups match
    for (auto const& pair : ref_map) {
        aether::RID tree_val;
        EXPECT_TRUE(tree.GetValue(pair.first, &tree_val));
        EXPECT_EQ(tree_val.page_id, pair.second.page_id);
        EXPECT_EQ(tree_val.slot_num, pair.second.slot_num);
    }

    // Phase C: Random Deletions (500 elements)
    std::shuffle(keys.begin(), keys.end(), rng);
    size_t delete_count = std::min(keys.size(), size_t(500));
    for (size_t i = 0; i < delete_count; ++i) {
        int64_t key = keys[i];
        bool tree_deleted = tree.Delete(key);
        bool map_deleted = ref_map.erase(key) > 0;
        EXPECT_EQ(tree_deleted, map_deleted);
    }

    // Phase D: Final verification of remaining lookups
    for (auto const& pair : ref_map) {
        aether::RID tree_val;
        EXPECT_TRUE(tree.GetValue(pair.first, &tree_val));
        EXPECT_EQ(tree_val.page_id, pair.second.page_id);
    }
}

int main() {
    int passed = 0;
    int failed = 0;
    auto& tests = aether::test::GetTests();
    std::cout << "[==========] Running " << tests.size() << " B+ Tree tests." << std::endl;
    for (const auto& test : tests) {
        std::cout << "[ RUN      ] " << test.suite_name << "." << test.test_name << std::endl;
        aether::test::GetPassCount() = 0;
        aether::test::GetFailCount() = 0;
        
        test.fn();
        
        if (aether::test::GetFailCount() > 0) {
            std::cout << "[  FAILED  ] " << test.suite_name << "." << test.test_name << std::endl;
            failed++;
        } else {
            std::cout << "[       OK ] " << test.suite_name << "." << test.test_name << std::endl;
            passed++;
        }
    }
    std::cout << "[==========] " << passed + failed << " tests ran." << std::endl;
    std::cout << "[  PASSED  ] " << passed << " tests." << std::endl;
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests." << std::endl;
        return 1;
    }
    return 0;
}
