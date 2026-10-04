#pragma once

#include <stdint.h>

namespace rawline::boot {

inline constexpr uint64_t boot_info_version = 4;
inline constexpr uint64_t boot_modules_version = 1;
inline constexpr uint64_t boot_module_version = 2;
inline constexpr uint64_t boot_memory_version = 3;
inline constexpr uint64_t boot_memory_map_version = 1;

struct FramebufferInfo {
    uint64_t address;

    uint64_t width;
    uint64_t height;
    uint64_t pitch;

    uint32_t bits_per_pixel;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;

    uint8_t green_mask_size;
    uint8_t green_mask_shift;

    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct BootModule {
    uint64_t version;
    uint64_t id;
    uint32_t generation;
    uint32_t reserved;
    const char* name;
    const void* address;
    uint64_t size;
};

struct BootModules {
    uint64_t version;
    uint64_t count;
    const BootModule* entries;
};

struct BootMemory {
    uint64_t version;
    uint64_t hhdm_offset;
    uint64_t base_physical;
    uint64_t next_physical;
    uint64_t end_physical;
};

struct BootMemoryRange {
    uint64_t base;
    uint64_t length;
};

struct BootMemoryMap {
    uint64_t version;
    uint64_t count;
    const BootMemoryRange* usable_ranges;
};

struct BootInfo {
    uint64_t version;

    FramebufferInfo framebuffer;
    BootModules modules;
    BootMemory memory;
    BootMemoryMap memory_map;
};

}
