#include "storage/disk_manager.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace aether {

DiskManager::DiskManager(const std::string& db_file) 
    : file_name_(db_file), free_list_head_(INVALID_PAGE_ID), num_pages_(0) {
    
    // Attempt to open existing file in binary read/write mode
    db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    
    if (!db_io_.is_open()) {
        // File does not exist, create it
        spdlog::info("Database file '{}' not found. Creating a new one.", file_name_);
        db_io_.open(file_name_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Failed to create database file: " + file_name_);
        }
        db_io_.close();
        
        // Re-open in read/write mode
        db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Failed to re-open database file: " + file_name_);
        }
        
        // Initialize Metadata (Page 0)
        free_list_head_ = INVALID_PAGE_ID;
        num_pages_ = 1; // Page 0 itself is the first page
        
        char metadata_page[PAGE_SIZE];
        std::memset(metadata_page, 0, PAGE_SIZE);
        
        // Write free list head and number of pages into page header
        std::memcpy(metadata_page, &free_list_head_, sizeof(free_list_head_));
        std::memcpy(metadata_page + sizeof(free_list_head_), &num_pages_, sizeof(num_pages_));
        
        db_io_.write(metadata_page, PAGE_SIZE);
        db_io_.flush();
        spdlog::info("Database file '{}' initialized successfully with Page 0.", file_name_);
    } else {
        // Load metadata from existing file
        ReadMetadata();
        spdlog::info("Loaded existing database '{}' with {} pages. Free list head: {}", 
                     file_name_, num_pages_, free_list_head_);
    }
}

DiskManager::~DiskManager() {
    Close();
}

void DiskManager::Close() {
    LockGuard lock(db_io_lck_);
    if (db_io_.is_open()) {
        db_io_.close();
        spdlog::info("Database file '{}' closed.", file_name_);
    }
}

void DiskManager::ReadPage(page_id_t page_id, char* out_buffer) {
    LockGuard lock(db_io_lck_);
    
    if (page_id >= num_pages_) {
        spdlog::error("ReadPage out of bounds: requested page_id {}, but num_pages is {}", page_id, num_pages_);
        throw std::out_of_range("ReadPage page_id out of bounds");
    }
    
    db_io_.clear(); // Clear any error flags
    db_io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    if (!db_io_.read(out_buffer, PAGE_SIZE)) {
        // If read failed, fill remaining with zero
        spdlog::warn("ReadPage failed to read full page for page_id {}. Padding with zeros.", page_id);
        std::memset(out_buffer, 0, PAGE_SIZE);
    }
}

void DiskManager::WritePage(page_id_t page_id, const char* data) {
    LockGuard lock(db_io_lck_);
    
    if (page_id >= num_pages_) {
        spdlog::error("WritePage out of bounds: target page_id {}, but num_pages is {}", page_id, num_pages_);
        throw std::out_of_range("WritePage page_id out of bounds");
    }
    
    db_io_.clear(); // Clear any error flags
    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    db_io_.write(data, PAGE_SIZE);
    db_io_.flush();
}

page_id_t DiskManager::AllocatePage() {
    LockGuard lock(db_io_lck_);
    
    page_id_t allocated_id;
    
    if (free_list_head_ != INVALID_PAGE_ID) {
        // Reuse a page from the free list
        allocated_id = free_list_head_;
        
        // Read the first 4 bytes of the reused page to find the next free page ID
        db_io_.clear();
        db_io_.seekg(static_cast<std::streamoff>(allocated_id) * PAGE_SIZE, std::ios::beg);
        
        page_id_t next_free_page;
        if (!db_io_.read(reinterpret_cast<char*>(&next_free_page), sizeof(next_free_page))) {
            spdlog::critical("Failed to read next free page ID from page {}", allocated_id);
            throw std::runtime_error("Corrupted database free list");
        }
        
        // Update free list head
        free_list_head_ = next_free_page;
        WriteMetadata();
        
        spdlog::debug("Allocated page {} from free list. New free list head: {}", allocated_id, free_list_head_);
    } else {
        // Grow the file by appending a new page
        allocated_id = num_pages_;
        num_pages_++;
        
        // Write Metadata update
        WriteMetadata();
        
        // Initialize the new page with zeros
        char blank_page[PAGE_SIZE];
        std::memset(blank_page, 0, PAGE_SIZE);
        
        db_io_.clear();
        db_io_.seekp(static_cast<std::streamoff>(allocated_id) * PAGE_SIZE, std::ios::beg);
        db_io_.write(blank_page, PAGE_SIZE);
        db_io_.flush();
        
        spdlog::debug("Grew database file. Allocated new page {}. Total pages: {}", allocated_id, num_pages_);
    }
    
    return allocated_id;
}

void DiskManager::DeallocatePage(page_id_t page_id) {
    LockGuard lock(db_io_lck_);
    
    if (page_id >= num_pages_ || page_id == 0) {
        spdlog::error("Invalid page deallocation: page_id {}. Total pages: {}", page_id, num_pages_);
        return; // Prevent deallocating metadata page or out of bounds page
    }
    
    // Prepare blank page where first 4 bytes point to the current free list head
    char page_data[PAGE_SIZE];
    std::memset(page_data, 0, PAGE_SIZE);
    std::memcpy(page_data, &free_list_head_, sizeof(free_list_head_));
    
    // Write link to free list into the page
    db_io_.clear();
    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    db_io_.write(page_data, PAGE_SIZE);
    db_io_.flush();
    
    // Set this page as the new free list head
    free_list_head_ = page_id;
    WriteMetadata();
    
    spdlog::debug("Deallocated page {}. New free list head: {}", page_id, free_list_head_);
}

uint32_t DiskManager::GetNumPages() const {
    LockGuard lock(db_io_lck_);
    return num_pages_;
}

void DiskManager::ReadMetadata() {
    db_io_.clear();
    db_io_.seekg(0, std::ios::beg);
    
    if (!db_io_.read(reinterpret_cast<char*>(&free_list_head_), sizeof(free_list_head_)) ||
        !db_io_.read(reinterpret_cast<char*>(&num_pages_), sizeof(num_pages_))) {
        spdlog::critical("Failed to read database metadata from Page 0!");
        throw std::runtime_error("Failed to read database metadata");
    }
}

void DiskManager::WriteMetadata() {
    db_io_.clear();
    db_io_.seekp(0, std::ios::beg);
    
    db_io_.write(reinterpret_cast<const char*>(&free_list_head_), sizeof(free_list_head_));
    db_io_.write(reinterpret_cast<const char*>(&num_pages_), sizeof(num_pages_));
    db_io_.flush();
}

} // namespace aether
