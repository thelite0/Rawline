#pragma once

#include <stdint.h>

#include <rawline/kernel_module.hpp>
#include <rawline/memory_module.hpp>

namespace rawline::kernel {

const boot::BootModule* find_boot_module(const boot::BootInfo* boot_info, const char* name);
int64_t start_rwl_module(const boot::BootInfo* boot_info,
    const boot::BootModule* module, const void* context);
bool get_memory_stats(const boot::BootInfo* boot_info, memory_module::Stats* stats);
bool get_memory_owner_stats(const boot::BootInfo* boot_info, uint64_t index,
    memory_module::OwnerStats* stats);
uint64_t get_memory_owner_allocations(const boot::BootInfo* boot_info,
    resource::OwnerId owner, uint64_t start_index,
    memory_module::AllocationInfo* output, uint64_t capacity);
resource::ResourceDomain create_memory_domain(resource::OwnerKind kind);
resource::AllocationHandle allocate_memory_pages(resource::OwnerId owner, uint64_t pages, uint32_t flags);
bool release_memory_allocation(resource::AllocationHandle handle);
bool destroy_memory_domain(resource::OwnerId owner);
bool transfer_memory_owner(resource::OwnerId old_owner, resource::OwnerId new_owner);
bool retain_shared_memory(resource::AllocationHandle handle);
bool release_shared_memory(resource::AllocationHandle handle);

}
