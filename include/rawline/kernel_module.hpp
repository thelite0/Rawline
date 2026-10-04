#pragma once

#include <stdint.h>

#include <rawline/boot_info.hpp>
#include <rawline/display_module.hpp>
#include <rawline/input_module.hpp>
#include <rawline/block_module.hpp>
#include <rawline/memory_module.hpp>
#include <rawline/pointer_event.hpp>
#include <rawline/wall_clock.hpp>
#include <rawline/pointer_event.hpp>

namespace rawline::kernel_module {

inline constexpr uint64_t api_version = 5;
inline constexpr uint64_t context_version = 5;

struct Api {
    uint64_t version;
    uint64_t size;
    void (*write_serial)(const char* text);
    const boot::BootModule* (*find_module)(const boot::BootInfo* boot_info, const char* name);
    int64_t (*start_module)(const boot::BootInfo* boot_info,
        const boot::BootModule* module, const void* context);
    bool (*get_memory_stats)(const boot::BootInfo* boot_info, memory_module::Stats* stats);
    bool (*get_owner_stats)(const boot::BootInfo* boot_info, uint64_t index,
        memory_module::OwnerStats* stats);
    uint64_t (*get_owner_allocations)(const boot::BootInfo* boot_info,
        resource::OwnerId owner, uint64_t start_index,
        memory_module::AllocationInfo* output, uint64_t capacity);
    resource::ResourceDomain (*create_domain)(resource::OwnerKind kind);
    resource::AllocationHandle (*allocate_pages)(resource::OwnerId owner, uint64_t pages, uint32_t flags);
    bool (*release_allocation)(resource::AllocationHandle handle);
    bool (*destroy_owner_domain)(resource::OwnerId owner);
    bool (*transfer_owner)(resource::OwnerId old_owner, resource::OwnerId new_owner);
    bool (*retain_shared)(resource::AllocationHandle handle);
    bool (*release_shared)(resource::AllocationHandle handle);
    bool (*initialize_block_device)(const boot::BootInfo* boot_info, resource::OwnerId buffer_owner);
    bool (*block_device_info)(uint64_t device, block_module::DeviceInfo* info);
    bool (*block_read)(uint64_t device, uint64_t lba, uint32_t count,
        resource::AllocationHandle buffer, uint64_t offset);
    bool (*block_write)(uint64_t device, uint64_t lba, uint32_t count,
        resource::AllocationHandle buffer, uint64_t offset);
    bool (*block_flush)(uint64_t device);
    bool (*allocate_dma_buffer)(resource::OwnerId owner, uint64_t pages,
        resource::AllocationHandle* handle, void** address);
    bool (*release_dma_buffer)(resource::AllocationHandle handle);
    bool (*read_wall_clock)(wall_clock::Time* time);
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
    const boot::BootInfo* boot_info;
    const display_module::Api* display_api;
    const input_module::Api* input_api;
    const graphics_backend::Api* graphics_api;
};

using EntryPoint = int64_t (*)(const Context* context);

}
