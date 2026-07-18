#include "storage/disk_manager.hpp"
#include "storage/replacer.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/table_page.hpp"
#include "storage/table_heap.hpp"
#include "storage/table_catalog.hpp"
#include "common/logger.hpp"
#include "aether_test.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

class TablePageTest : public ::testing::Test {
protected:
    const std::string db_filename = "test_table_page.db";

    void SetUp() override {
        aether::InitializeLogger();
        std::remove(db_filename.c_str());
    }

    void TearDown() override {
        std::remove(db_filename.c_str());
    }
};

TEST_F(TablePageTest, TupleSerializationDeserialization) {
    std::vector<aether::Column> cols;
    cols.emplace_back("id", aether::TypeId::INT);
    cols.emplace_back("name", aether::TypeId::VARCHAR, 100);
    cols.emplace_back("active", aether::TypeId::BOOLEAN);
    aether::Schema schema(cols);

    std::vector<aether::Value> vals;
    vals.emplace_back(42);
    vals.emplace_back("AetherDB");
    vals.emplace_back(true);
    aether::Tuple tuple(vals);

    uint32_t len = tuple.GetSerializedLength();
    std::vector<char> buffer(len);
    tuple.Serialize(buffer.data());

    aether::Tuple deserialized;
    deserialized.Deserialize(buffer.data(), schema);

    EXPECT_EQ(deserialized.GetValue(0).GetAsInt(), 42);
    EXPECT_EQ(deserialized.GetValue(1).GetAsVarChar(), "AetherDB");
    EXPECT_EQ(deserialized.GetValue(2).GetAsBool(), true);
}

TEST_F(TablePageTest, SlottedPageInsertAndGet) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(10);
    aether::BufferPoolManager bpm(10, disk_manager.get(), &replacer);

    std::vector<aether::Column> cols;
    cols.emplace_back("id", aether::TypeId::INT);
    cols.emplace_back("bio", aether::TypeId::VARCHAR, 200);
    aether::Schema schema(cols);

    aether::page_id_t page_id;
    aether::Page *page = bpm.NewPage(&page_id);
    EXPECT_TRUE(page != nullptr);

    auto *table_page = reinterpret_cast<aether::TablePage*>(page->GetData());
    table_page->Init(page_id);

    // Create a tuple
    std::vector<aether::Value> vals1 = {aether::Value(1), aether::Value("Hello World")};
    aether::Tuple tuple1(vals1);

    // Create another tuple
    std::vector<aether::Value> vals2 = {aether::Value(2), aether::Value("Systems engineering is fun")};
    aether::Tuple tuple2(vals2);

    uint32_t slot1, slot2;
    EXPECT_TRUE(table_page->InsertTuple(tuple1, schema, &slot1));
    EXPECT_TRUE(table_page->InsertTuple(tuple2, schema, &slot2));
    EXPECT_EQ(slot1, 0);
    EXPECT_EQ(slot2, 1);

    // Read back
    aether::Tuple read1, read2;
    EXPECT_TRUE(table_page->GetTuple(0, &read1, schema));
    EXPECT_TRUE(table_page->GetTuple(1, &read2, schema));

    EXPECT_EQ(read1.GetValue(0).GetAsInt(), 1);
    EXPECT_EQ(read1.GetValue(1).GetAsVarChar(), "Hello World");

    EXPECT_EQ(read2.GetValue(0).GetAsInt(), 2);
    EXPECT_EQ(read2.GetValue(1).GetAsVarChar(), "Systems engineering is fun");

    bpm.UnpinPage(page_id, true);
}

TEST_F(TablePageTest, TombstoneAndSlotReuse) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(10);
    aether::BufferPoolManager bpm(10, disk_manager.get(), &replacer);

    std::vector<aether::Column> cols = {aether::Column("bio", aether::TypeId::VARCHAR, 100)};
    aether::Schema schema(cols);

    aether::page_id_t page_id;
    aether::Page *page = bpm.NewPage(&page_id);
    auto *table_page = reinterpret_cast<aether::TablePage*>(page->GetData());
    table_page->Init(page_id);

    aether::Tuple t1({aether::Value("A")});
    aether::Tuple t2({aether::Value("B")});
    aether::Tuple t3({aether::Value("C")});

    uint32_t s1, s2, s3;
    table_page->InsertTuple(t1, schema, &s1);
    table_page->InsertTuple(t2, schema, &s2);
    
    EXPECT_EQ(s1, 0);
    EXPECT_EQ(s2, 1);

    // Delete t2 (slot 1)
    EXPECT_TRUE(table_page->DeleteTuple(s2));

    // Get on deleted should fail
    aether::Tuple temp;
    EXPECT_FALSE(table_page->GetTuple(s2, &temp, schema));

    // Next insert should reuse slot 1 instead of appending
    table_page->InsertTuple(t3, schema, &s3);
    EXPECT_EQ(s3, 1);

    EXPECT_TRUE(table_page->GetTuple(1, &temp, schema));
    EXPECT_EQ(temp.GetValue(0).GetAsVarChar(), "C");

    bpm.UnpinPage(page_id, true);
}

TEST_F(TablePageTest, InPlaceAndRelocateUpdates) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(10);
    aether::BufferPoolManager bpm(10, disk_manager.get(), &replacer);

    std::vector<aether::Column> cols = {aether::Column("data", aether::TypeId::VARCHAR, 200)};
    aether::Schema schema(cols);

    aether::page_id_t page_id;
    aether::Page *page = bpm.NewPage(&page_id);
    auto *table_page = reinterpret_cast<aether::TablePage*>(page->GetData());
    table_page->Init(page_id);

    aether::Tuple t1({aether::Value("Short string")});
    uint32_t s1;
    table_page->InsertTuple(t1, schema, &s1);

    // Update in-place with smaller string
    aether::Tuple t2({aether::Value("Tiny")});
    EXPECT_TRUE(table_page->UpdateTuple(s1, t2, schema));

    aether::Tuple read;
    EXPECT_TRUE(table_page->GetTuple(s1, &read, schema));
    EXPECT_EQ(read.GetValue(0).GetAsVarChar(), "Tiny");

    // Update with much larger string requiring relocation
    aether::Tuple t3({aether::Value("This is a significantly longer string that requires new allocation on the slotted page")});
    EXPECT_TRUE(table_page->UpdateTuple(s1, t3, schema));

    EXPECT_TRUE(table_page->GetTuple(s1, &read, schema));
    EXPECT_EQ(read.GetValue(0).GetAsVarChar(), "This is a significantly longer string that requires new allocation on the slotted page");

    bpm.UnpinPage(page_id, true);
}

TEST_F(TablePageTest, TableHeapLinkageAndOverflow) {
    auto disk_manager = std::make_unique<aether::DiskManager>(db_filename);
    aether::LRUReplacer replacer(50);
    aether::BufferPoolManager bpm(50, disk_manager.get(), &replacer);

    std::vector<aether::Column> cols = {
        aether::Column("id", aether::TypeId::INT),
        aether::Column("desc", aether::TypeId::VARCHAR, 500)
    };
    aether::Schema schema(cols);

    aether::TableHeap heap(&bpm);

    // Insert tuples until page overflow forces new page allocations
    // Each tuple takes ~220 bytes. We insert 40 tuples (~8.8KB), which needs at least 3 pages.
    std::string large_desc(200, 'x');
    std::vector<aether::RID> rids;

    for (int i = 1; i <= 40; ++i) {
        aether::Tuple tuple({aether::Value(i), aether::Value(large_desc)});
        aether::RID rid;
        EXPECT_TRUE(heap.InsertTuple(tuple, schema, &rid));
        rids.push_back(rid);
    }

    // Verify all pages were linked and tuples are correct
    for (int i = 0; i < 40; ++i) {
        aether::Tuple tuple;
        EXPECT_TRUE(heap.GetTuple(rids[i], &tuple, schema));
        EXPECT_EQ(tuple.GetValue(0).GetAsInt(), i + 1);
        EXPECT_EQ(tuple.GetValue(1).GetAsVarChar(), large_desc);
    }

    // Check that we have multiple pages
    EXPECT_TRUE(rids.back().page_id > rids.front().page_id);
}

TEST_F(TablePageTest, TableCatalogRouting) {
    aether::TableCatalog catalog;
    
    std::vector<aether::Column> cols = {aether::Column("id", aether::TypeId::INT)};
    aether::Schema schema(cols);

    catalog.CreateTable("users", schema, 10, 100);

    aether::TableMetadata meta;
    EXPECT_TRUE(catalog.GetTableMetadata("users", &meta));
    EXPECT_EQ(meta.name, "users");
    EXPECT_EQ(meta.first_page_id, 10);
    EXPECT_EQ(meta.index_root_id, 100);

    // Non-existent table
    EXPECT_FALSE(catalog.GetTableMetadata("non_existent", &meta));

    // Update index root
    catalog.UpdateIndexRoot("users", 200);
    EXPECT_TRUE(catalog.GetTableMetadata("users", &meta));
    EXPECT_EQ(meta.index_root_id, 200);
}

int main() {
    int passed = 0;
    int failed = 0;
    auto& tests = aether::test::GetTests();
    std::cout << "[==========] Running " << tests.size() << " TablePage and TableHeap tests." << std::endl;
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
