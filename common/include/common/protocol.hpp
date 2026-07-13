#pragma once

#include <cstdint>

#ifdef ERROR
#undef ERROR
#endif

namespace aether {
namespace protocol {

// Operation Types
enum class OpType : uint8_t {
    READ_PAGE = 0,
    WRITE_PAGE = 1,
    ALLOCATE_PAGE = 2,
    DEALLOCATE_PAGE = 3
};

// Status Codes
enum class StatusCode : uint8_t {
    SUCCESS = 0,
    ERROR = 1
};

#pragma pack(push, 1)
struct RequestHeader {
    uint8_t op_type;    // Cast of OpType
    uint32_t page_id;   // Target page_id
    uint32_t data_len;  // Length of the following data payload (e.g. PAGE_SIZE for write)
};

struct ResponseHeader {
    uint8_t status;     // Cast of StatusCode
    uint32_t page_id;   // Allocated page_id or relevant info
    uint32_t data_len;  // Length of the following data payload (e.g. PAGE_SIZE for read)
};
#pragma pack(pop)

} // namespace protocol
} // namespace aether
