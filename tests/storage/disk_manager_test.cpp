#include "storage/disk_manager.hpp"
#include "common/logger.hpp"
#include "aether_test.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

class DiskManagerTest : public ::testing::Test {
protected:
    const std::string db_filename = "test_aether.db";

    void SetUp() override {
        // Initialize logger for testing output
        aether::InitializeLogger();
        // Remove any stale test db file
        std::remove(db_filename.c_str());
    }

    void TearDown() override {
        // Clean up the test database file
        std::remove(db_filename.c_str());
    }
};

TEST_F(DiskManagerTest, PageAllocationAndDeallocation) {
    // 1. Create DiskManager
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    EXPECT_EQ(disk_manager->GetNumPages(), 1); // Only page 0 metadata

    // Allocate 3 pages
    aether::page_id_t p1 = disk_manager->AllocatePage();
    aether::page_id_t p2 = disk_manager->AllocatePage();
    aether::page_id_t p3 = disk_manager->AllocatePage();

    EXPECT_EQ(p1, 1);
    EXPECT_EQ(p2, 2);
    EXPECT_EQ(p3, 3);
    EXPECT_EQ(disk_manager->GetNumPages(), 4); // Page 0 + 3 allocated pages

    // 2. Deallocate page 2
    disk_manager->DeallocatePage(p2);
    // Total pages in file should still be 4
    EXPECT_EQ(disk_manager->GetNumPages(), 4);

    // 3. Allocate again, should reuse page 2 from free list
    aether::page_id_t p4 = disk_manager->AllocatePage();
    EXPECT_EQ(p4, p2); // Should reuse page 2
    EXPECT_EQ(disk_manager->GetNumPages(), 4); // Still 4 pages

    // Allocate another one, should grow file to page 4
    aether::page_id_t p5 = disk_manager->AllocatePage();
    EXPECT_EQ(p5, 4);
    EXPECT_EQ(disk_manager->GetNumPages(), 5);
}

TEST_F(DiskManagerTest, ReadWritePersistence) {
    // 1. Create and write to pages
    {
        aether::DiskManager disk_manager(db_filename);
        aether::page_id_t p1 = disk_manager.AllocatePage();
        aether::page_id_t p2 = disk_manager.AllocatePage();

        char write_buf1[aether::PAGE_SIZE];
        char write_buf2[aether::PAGE_SIZE];
        std::memset(write_buf1, 'A', aether::PAGE_SIZE);
        std::memset(write_buf2, 'B', aether::PAGE_SIZE);

        disk_manager.WritePage(p1, write_buf1);
        disk_manager.WritePage(p2, write_buf2);
    } // Closes the disk manager and flushes all pages to disk

    // 2. Re-open and verify contents read back correctly
    {
        aether::DiskManager disk_manager(db_filename);
        EXPECT_EQ(disk_manager.GetNumPages(), 3); // Page 0 + 2 allocated pages

        char read_buf1[aether::PAGE_SIZE];
        char read_buf2[aether::PAGE_SIZE];

        disk_manager.ReadPage(1, read_buf1);
        disk_manager.ReadPage(2, read_buf2);

        for (size_t i = 0; i < aether::PAGE_SIZE; ++i) {
            EXPECT_EQ(read_buf1[i], 'A');
            EXPECT_EQ(read_buf2[i], 'B');
        }
    }
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
