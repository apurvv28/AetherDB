#include "storage/disk_manager.hpp"
#include "storage/replacer.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "common/logger.hpp"
#include "aether_test.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

class BufferPoolManagerTest : public ::testing::Test {
protected:
    const std::string db_filename = "test_buffer_pool.db";

    void SetUp() override {
        aether::InitializeLogger();
        std::remove(db_filename.c_str());
    }

    void TearDown() override {
        std::remove(db_filename.c_str());
    }
};

TEST_F(BufferPoolManagerTest, LRUReplacerSequence) {
    aether::LRUReplacer replacer(4);

    // Unpin 0, 1, 2, 3 in order
    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    EXPECT_EQ(replacer.Size(), 4);

    // Pin 2 (removes from replacer)
    replacer.Pin(2);
    EXPECT_EQ(replacer.Size(), 3);

    // Unpin 2 (goes to the back of LRU queue)
    replacer.Unpin(2);
    EXPECT_EQ(replacer.Size(), 4);

    // Eviction order: least-recently unpinned should be first
    aether::frame_id_t victim;
    
    // First victim should be 0
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 0);

    // Second victim should be 1
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);

    // Third victim should be 3 (since 2 was pinned and unpinned, moving it to the back)
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 3);

    // Fourth victim should be 2
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);

    // Replacer should now be empty
    EXPECT_EQ(replacer.Size(), 0);
    EXPECT_FALSE(replacer.Victim(&victim));
}

TEST_F(BufferPoolManagerTest, ClockReplacerSequence) {
    aether::ClockReplacer replacer(4);

    // Unpin 0, 1, 2, 3 in order (each gets ref_flag = true, in_replacer = true)
    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    EXPECT_EQ(replacer.Size(), 4);

    // Pin 2 (removes 2 from replacer)
    replacer.Pin(2);
    EXPECT_EQ(replacer.Size(), 3); // replacer has: 0, 1, 3

    // First victim selection:
    // Hand starts at 0.
    // - 0 is in replacer, ref_flag=true -> set ref_flag=false, hand -> 1
    // - 1 is in replacer, ref_flag=true -> set ref_flag=false, hand -> 2
    // - 2 is not in replacer -> hand -> 3
    // - 3 is in replacer, ref_flag=true -> set ref_flag=false, hand -> 0
    // - 0 is in replacer, ref_flag=false -> victim! hand -> 1
    aether::frame_id_t victim;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 0);
    EXPECT_EQ(replacer.Size(), 2); // replacer has: 1, 3

    // Second victim selection:
    // Hand is at 1.
    // - 1 is in replacer, ref_flag=false -> victim! hand -> 2
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
    EXPECT_EQ(replacer.Size(), 1); // replacer has: 3

    // Unpin 0 again (ref_flag = true, in_replacer = true)
    replacer.Unpin(0);
    EXPECT_EQ(replacer.Size(), 2); // replacer has: 3 (ref_flag=false), 0 (ref_flag=true)

    // Third victim selection:
    // Hand is at 2.
    // - 2 is not in replacer -> hand -> 3
    // - 3 is in replacer, ref_flag=false -> victim! hand -> 0
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 3);
    EXPECT_EQ(replacer.Size(), 1); // replacer has: 0 (ref_flag=true)

    // Fourth victim selection:
    // Hand is at 0.
    // - 0 is in replacer, ref_flag=true -> set ref_flag=false, hand -> 1
    // - 1 is not in replacer -> hand -> 2
    // - 2 is not in replacer -> hand -> 3
    // - 3 is not in replacer -> hand -> 0
    // - 0 is in replacer, ref_flag=false -> victim! hand -> 1
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 0);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST_F(BufferPoolManagerTest, BufferPoolBasicOperations) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(10);
    aether::BufferPoolManager bpm(10, disk_manager.get(), &replacer);

    aether::page_id_t p0_id, p1_id;
    
    // Allocate new pages
    aether::Page* p0 = bpm.NewPage(&p0_id);
    aether::Page* p1 = bpm.NewPage(&p1_id);

    EXPECT_TRUE(p0 != nullptr);
    EXPECT_TRUE(p1 != nullptr);
    EXPECT_EQ(p0_id, 1);
    EXPECT_EQ(p1_id, 2);
    EXPECT_EQ(p0->GetPinCount(), 1);
    EXPECT_EQ(p1->GetPinCount(), 1);

    // Write to p0
    std::strcpy(p0->GetData(), "AetherDB Page 0 Content");
    
    // Unpin p0 as dirty
    EXPECT_TRUE(bpm.UnpinPage(p0_id, true));
    EXPECT_EQ(p0->GetPinCount(), 0);

    // Fetch p0 again
    aether::Page* p0_fetched = bpm.FetchPage(p0_id);
    EXPECT_TRUE(p0_fetched != nullptr);
    EXPECT_EQ(p0_fetched->GetPinCount(), 1);
    EXPECT_EQ(std::strcmp(p0_fetched->GetData(), "AetherDB Page 0 Content"), 0);

    // Unpin again as clean
    EXPECT_TRUE(bpm.UnpinPage(p0_id, false));
}

TEST_F(BufferPoolManagerTest, EvictionUnderMemoryPressure) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(3);
    aether::BufferPoolManager bpm(3, disk_manager.get(), &replacer);

    aether::page_id_t p1_id, p2_id, p3_id, p4_id;
    
    aether::Page* p1 = bpm.NewPage(&p1_id); // pin count = 1
    aether::Page* p2 = bpm.NewPage(&p2_id); // pin count = 1
    aether::Page* p3 = bpm.NewPage(&p3_id); // pin count = 1

    EXPECT_TRUE(p1 != nullptr);
    EXPECT_TRUE(p2 != nullptr);
    EXPECT_TRUE(p3 != nullptr);

    // Write different content to each page
    std::strcpy(p1->GetData(), "Page 1 Content");
    std::strcpy(p2->GetData(), "Page 2 Content");
    std::strcpy(p3->GetData(), "Page 3 Content");

    // Try allocating page 4 while all 3 frames are pinned. Should fail.
    aether::Page* p4 = bpm.NewPage(&p4_id);
    EXPECT_TRUE(p4 == nullptr);
    EXPECT_EQ(p4_id, aether::INVALID_PAGE_ID);

    // Unpin page 1 (dirty) and page 2 (clean)
    EXPECT_TRUE(bpm.UnpinPage(p1_id, true));  // unpinned, dirty
    EXPECT_TRUE(bpm.UnpinPage(p2_id, false)); // unpinned, clean
    // Page 3 remains pinned!

    // Now, allocate page 4. Buffer pool has size 3, containing p1 (dirty, unpinned), p2 (clean, unpinned), p3 (pinned).
    // Using LRU replacer:
    // p1 was unpinned first, so it is the least-recently-used unpinned frame.
    // Therefore, p1 must be evicted to make room for p4!
    p4 = bpm.NewPage(&p4_id);
    EXPECT_TRUE(p4 != nullptr);
    EXPECT_EQ(p4_id, 4);

    // Verify p1 is evicted from cache, while p2 and p3 are still cached
    EXPECT_FALSE(bpm.IsCached(p1_id));
    EXPECT_TRUE(bpm.IsCached(p2_id));
    EXPECT_TRUE(bpm.IsCached(p3_id));

    // Verify p3 was not evicted (since it's pinned) and still has pin count = 1
    EXPECT_EQ(bpm.GetPinCount(p3_id), 1);

    // Since p1 was evicted and it was dirty, it should have been written to disk.
    // Let's check this by fetching p1 again. Fetching p1 will trigger eviction of p2 (which is now the only unpinned page).
    // Since p2 was unpinned as clean, its eviction should NOT write to disk (but it remains on disk as initialized blank or modified).
    // Wait, let's verify fetching p1 succeeds.
    aether::Page* p1_fetched = bpm.FetchPage(p1_id);
    EXPECT_TRUE(p1_fetched != nullptr);
    EXPECT_EQ(std::strcmp(p1_fetched->GetData(), "Page 1 Content"), 0); // Correctly persisted and loaded!
    
    // Now p2 should be evicted (clean)
    EXPECT_FALSE(bpm.IsCached(p2_id));

    // Clean up
    bpm.UnpinPage(p1_id, false);
    bpm.UnpinPage(p3_id, false);
    bpm.UnpinPage(p4_id, false);
}

int main() {
    int passed = 0;
    int failed = 0;
    auto& tests = aether::test::GetTests();
    std::cout << "[==========] Running " << tests.size() << " tests." << std::endl;
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
