#pragma once

#include "common/types.hpp"
#include "common/concurrency.hpp"
#include <string>
#include <fstream>

namespace aether {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Prevent copy and move
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;
    DiskManager(DiskManager&&) = delete;
    DiskManager& operator=(DiskManager&&) = delete;

    // Read a 4KB page from disk into out_buffer
    void ReadPage(page_id_t page_id, char* out_buffer);

    // Write a 4KB page from data buffer to disk
    void WritePage(page_id_t page_id, const char* data);

    // Allocate a new page (reuses free list or grows the file), returns new page_id
    page_id_t AllocatePage();

    // Deallocate a page, adding it to the free list for future reuse
    void DeallocatePage(page_id_t page_id);

    // Get total number of pages currently managed in the file (including page 0 and free list)
    uint32_t GetNumPages() const;

    // Close the database file explicitly
    void Close();

private:
    // Helper to read metadata (Page 0) from disk
    void ReadMetadata();

    // Helper to write metadata (Page 0) to disk
    void WriteMetadata();

    std::string file_name_;
    mutable std::fstream db_io_;
    mutable Mutex db_io_lck_;
    
    // Metadata cache (synchronized with Page 0)
    page_id_t free_list_head_;
    uint32_t num_pages_;
};

} // namespace aether
