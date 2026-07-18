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

struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_num{0};

    bool operator==(const RID &other) const {
        return page_id == other.page_id && slot_num == other.slot_num;
    }
    bool operator!=(const RID &other) const {
        return !(*this == other);
    }
};

} // namespace aether
