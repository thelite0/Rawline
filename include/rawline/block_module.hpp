#pragma once

#include <stdint.h>

#include <rawline/resource.hpp>

namespace rawline::block_module {

inline constexpr uint64_t api_version = 1;
inline constexpr uint64_t context_version = 1;
inline constexpr uint32_t sector_size = 512;
inline constexpr uint32_t device_flag_flush = 1;

struct DeviceInfo {
    uint64_t version;
    uint64_t id;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t flags;
};

struct Api {
    uint64_t version;
    uint64_t size;
    uint64_t (*device_count)();
    bool (*get_device_info)(uint64_t device, DeviceInfo* info);
    bool (*read_blocks)(uint64_t device, uint64_t lba, uint32_t count,
        resource::AllocationHandle buffer, uint64_t buffer_offset);
    bool (*write_blocks)(uint64_t device, uint64_t lba, uint32_t count,
        resource::AllocationHandle buffer, uint64_t buffer_offset);
    bool (*flush)(uint64_t device);
    bool (*allocate_buffer)(uint64_t pages, resource::AllocationHandle* handle,
        void** address);
    bool (*release_buffer)(resource::AllocationHandle handle);
    void (*write_serial)(const char* text);
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
};

}
