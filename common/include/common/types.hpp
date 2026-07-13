#pragma once

#include <cstdint>
#include <cstddef>

namespace aether {

// Core Types
using page_id_t = uint32_t;
using lsn_t = int32_t;
using txn_id_t = uint32_t;

// Constants
constexpr size_t PAGE_SIZE = 4096; // 4KB pages

constexpr page_id_t INVALID_PAGE_ID = 0xFFFFFFFF;
constexpr lsn_t INVALID_LSN = -1;
constexpr txn_id_t INVALID_TXN_ID = 0;

} // namespace aether
