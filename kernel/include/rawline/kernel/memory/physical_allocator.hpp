#pragma once

#include <stdint.h>

#include <rawline/boot_info.hpp>
#include <rawline/memory_module.hpp>
#include <rawline/resource.hpp>

namespace rawline::kernel::memory {

class PhysicalAllocator {
public:
    bool initialize(const boot::BootInfo* boot_info);
    bool claim_kernel_range(uint64_t base, uint64_t pages);
    bool reserve_kernel_range(uint64_t base, uint64_t pages);
    resource::ResourceDomain create_domain(resource::OwnerKind kind);
    resource::AllocationHandle allocate_pages(resource::OwnerId owner,
        uint64_t pages, uint32_t flags = 0);
    bool physical_address(resource::AllocationHandle handle, uint64_t* address) const;
    bool allocation_info(resource::AllocationHandle handle, memory_module::AllocationInfo* info) const;
    bool register_mapping(resource::AllocationHandle handle, uint64_t virtual_base,
        uint64_t physical_offset, uint64_t pages);
    bool release(resource::AllocationHandle handle);
    bool destroy_owner_domain(resource::OwnerId owner);
    bool transfer_ownership(resource::AllocationHandle handle, resource::OwnerId new_owner);
    bool transfer_owner(resource::OwnerId old_owner, resource::OwnerId new_owner);
    bool retain_shared(resource::AllocationHandle handle);
    bool release_shared(resource::AllocationHandle handle);
    bool query_owner(resource::OwnerId owner, memory_module::OwnerStats* stats) const;
    uint64_t owner_count() const;
    bool owner_at(uint64_t index, memory_module::OwnerStats* stats) const;
    uint64_t allocations_for_owner(resource::OwnerId owner,
        uint64_t start_index, memory_module::AllocationInfo* output, uint64_t capacity) const;
    bool statistics(memory_module::Stats* stats) const;

private:
    static constexpr uint64_t page_size = 0x1000;
    static constexpr uint32_t maximum_free_extents = 8192;
    static constexpr uint32_t maximum_allocations = 4096;
    static constexpr uint32_t maximum_mappings_per_allocation = 8;

    struct FreeExtent {
        uint64_t base;
        uint64_t pages;
    };

    struct Mapping {
        uint64_t virtual_base;
        uint64_t physical_offset;
        uint64_t pages;
    };

    struct Allocation {
        uint64_t base;
        uint64_t pages;
        resource::OwnerId owner;
        uint32_t generation;
        uint32_t flags;
        uint32_t shared_references;
        uint32_t mapping_count;
        Mapping mappings[maximum_mappings_per_allocation];
        bool active;
    };

    bool remove_free_range(uint64_t base, uint64_t pages);
    bool add_free_range(uint64_t base, uint64_t pages);
    int32_t free_slot() const;
    Allocation* find(resource::AllocationHandle handle);
    const Allocation* find(resource::AllocationHandle handle) const;
    bool reclaim(Allocation& allocation);
    bool unmap(const Mapping& mapping, uint64_t physical_base);
    bool same_owner(resource::OwnerId left, resource::OwnerId right) const;
    bool seen_owner(resource::OwnerId owner, uint64_t before_slot) const;

    const boot::BootInfo* boot_info_;
    FreeExtent free_extents_[maximum_free_extents];
    uint32_t free_extent_count_;
    Allocation allocations_[maximum_allocations];
    uint64_t next_domain_id_;
    uint64_t total_pages_;
    uint64_t free_pages_;
    bool initialized_;
};

PhysicalAllocator& physical_allocator();

}
