#pragma once

#include <stdint.h>

#include <rawline/boot_info.hpp>
#include <rawline/resource.hpp>

namespace rawline::memory_module {

inline constexpr uint64_t api_version = 2;
inline constexpr uint64_t context_version = 2;
inline constexpr uint64_t stats_version = 2;
inline constexpr uint64_t owner_stats_version = 1;
inline constexpr uint64_t allocation_info_version = 1;

struct OwnerStats {
    uint64_t version;
    resource::OwnerId owner;
    uint64_t allocation_count;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
};

struct AllocationInfo {
    uint64_t version;
    resource::AllocationHandle handle;
    resource::OwnerId owner;
    uint64_t physical_base;
    uint64_t pages;
    uint32_t flags;
    uint32_t shared_references;
};

struct Stats {
    uint64_t version;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t owner_count;
};

struct Api {
    uint64_t version;
    uint64_t size;
    bool (*get_stats)(const boot::BootInfo* boot_info, Stats* stats);
    bool (*get_owner_stats)(const boot::BootInfo* boot_info, uint64_t index, OwnerStats* stats);
    uint64_t (*get_owner_allocations)(const boot::BootInfo* boot_info,
        resource::OwnerId owner, uint64_t start_index, AllocationInfo* output, uint64_t capacity);
    resource::ResourceDomain (*create_domain)(resource::OwnerKind kind);
    resource::AllocationHandle (*allocate_pages)(resource::OwnerId owner, uint64_t pages, uint32_t flags);
    bool (*release_allocation)(resource::AllocationHandle handle);
    bool (*destroy_owner_domain)(resource::OwnerId owner);
    bool (*transfer_owner)(resource::OwnerId old_owner, resource::OwnerId new_owner);
    bool (*retain_shared)(resource::AllocationHandle handle);
    bool (*release_shared)(resource::AllocationHandle handle);
    void (*write_serial)(const char* text);
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
    const boot::BootInfo* boot_info;
    uint64_t attempt;
};

using EntryPoint = int64_t (*)(const Context* context);

}
