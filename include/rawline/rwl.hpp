#pragma once

#include <stdint.h>

namespace rawline::rwl {

inline constexpr uint64_t magic = 0x314c57524e494c52;
inline constexpr uint32_t version = 1;
inline constexpr uint16_t architecture_x86_64 = 1;
inline constexpr uint32_t image_flag_relocatable = 2;
inline constexpr uint32_t segment_flag_read = 1;
inline constexpr uint32_t segment_flag_write = 2;
inline constexpr uint32_t segment_flag_execute = 4;
inline constexpr uint32_t maximum_segments = 8;

struct Header {
    uint64_t magic;
    uint32_t header_size;
    uint32_t version;
    uint16_t architecture;
    uint16_t segment_count;
    uint32_t flags;
    uint64_t image_size;
    uint64_t entry_address;
    uint64_t preferred_virtual_base;
    uint64_t segments_offset;
};

struct Segment {
    uint64_t file_offset;
    uint64_t image_offset;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
    uint32_t reserved;
};

}
