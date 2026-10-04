#include <rawline/kernel/memory/physical_allocator.hpp>

namespace rawline::kernel::memory {
namespace {

constexpr uint64_t physical_mask = 0x000ffffffffff000ull;
constexpr resource::OwnerId kernel_owner{1, 1, resource::OwnerKind::Kernel};
PhysicalAllocator allocator;

uint64_t align_up(uint64_t value)
{
    return (value + 0xfff) & ~uint64_t{0xfff};
}

}

PhysicalAllocator& physical_allocator()
{
    return allocator;
}

bool PhysicalAllocator::initialize(const boot::BootInfo* boot_info)
{
    if (initialized_ || boot_info == nullptr ||
        boot_info->version != boot::boot_info_version ||
        boot_info->memory.version != boot::boot_memory_version ||
        boot_info->memory_map.version != boot::boot_memory_map_version ||
        boot_info->memory_map.usable_ranges == nullptr ||
        boot_info->memory_map.count == 0 || boot_info->memory_map.count > maximum_free_extents ||
        boot_info->memory.next_physical < boot_info->memory.base_physical ||
        (boot_info->memory.base_physical & 0xfff) != 0 ||
        (boot_info->memory.next_physical & 0xfff) != 0) return false;

    boot_info_ = boot_info;
    free_extent_count_ = 0;
    total_pages_ = 0;
    free_pages_ = 0;
    next_domain_id_ = 1;
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        allocations_[i].active = false;
        allocations_[i].generation = 0;
    }

    for (uint64_t i = 0; i < boot_info->memory_map.count; ++i) {
        const auto& range = boot_info->memory_map.usable_ranges[i];
        if (range.length > UINT64_MAX - range.base) return false;
        uint64_t base = align_up(range.base < 0x100000 ? 0x100000 : range.base);
        const uint64_t end = (range.base + range.length) & ~uint64_t{0xfff};
        if (base >= end) continue;
        if (end > (uint64_t{1} << 52)) return false;
        const uint64_t pages = (end - base) / page_size;
        if (!add_free_range(base, pages)) return false;
        total_pages_ += pages;
    }
    if (total_pages_ == 0) return false;
    free_pages_ = total_pages_;
    initialized_ = true;
    const uint64_t bootstrap_pages = (boot_info->memory.next_physical - boot_info->memory.base_physical) / page_size;
    if (bootstrap_pages == 0 || !claim_kernel_range(boot_info->memory.base_physical, bootstrap_pages)) {
        initialized_ = false;
        return false;
    }
    return true;
}

bool PhysicalAllocator::claim_kernel_range(uint64_t base, uint64_t pages)
{
    if (!initialized_ || pages == 0 || pages > free_pages_ || (base & 0xfff) != 0) return false;
    const int32_t slot = free_slot();
    if (slot < 0 || !remove_free_range(base, pages)) return false;
    Allocation& allocation = allocations_[slot];
    ++allocation.generation;
    if (allocation.generation == 0) ++allocation.generation;
    allocation.base = base;
    allocation.pages = pages;
    allocation.owner = kernel_owner;
    allocation.flags = resource::allocation_flag_kernel_lifetime;
    allocation.shared_references = 0;
    allocation.mapping_count = 0;
    allocation.active = true;
    free_pages_ -= pages;
    return true;
}

resource::ResourceDomain PhysicalAllocator::create_domain(resource::OwnerKind kind)
{
    if (!initialized_ || kind == resource::OwnerKind::Kernel || next_domain_id_ == UINT64_MAX)
        return {};
    return {next_domain_id_++, 1, kind};
}

resource::AllocationHandle PhysicalAllocator::allocate_pages(resource::OwnerId owner,
    uint64_t pages, uint32_t flags)
{
    if (!initialized_ || owner.value == 0 || owner.generation == 0 || pages == 0 ||
        pages > UINT64_MAX / page_size || pages > free_pages_ ||
        (flags & ~(resource::allocation_flag_kernel_lifetime | resource::allocation_flag_shared)) != 0 ||
        ((flags & resource::allocation_flag_kernel_lifetime) && owner.kind != resource::OwnerKind::Kernel) ||
        ((flags & resource::allocation_flag_shared) &&
            (flags & resource::allocation_flag_kernel_lifetime)) ||
        ((flags & resource::allocation_flag_shared) && owner.kind != resource::OwnerKind::Shared)) return {};
    const int32_t slot = free_slot();
    if (slot < 0) return {};
    uint32_t extent_index = free_extent_count_;
    for (uint32_t i = 0; i < free_extent_count_; ++i) {
        if (free_extents_[i].pages >= pages) { extent_index = i; break; }
    }
    if (extent_index == free_extent_count_) return {};
    const uint64_t base = free_extents_[extent_index].base;
    free_extents_[extent_index].base += pages * page_size;
    free_extents_[extent_index].pages -= pages;
    if (free_extents_[extent_index].pages == 0) {
        for (uint32_t i = extent_index + 1; i < free_extent_count_; ++i)
            free_extents_[i - 1] = free_extents_[i];
        --free_extent_count_;
    }
    free_pages_ -= pages;
    Allocation& allocation = allocations_[slot];
    ++allocation.generation;
    if (allocation.generation == 0) ++allocation.generation;
    allocation.base = base;
    allocation.pages = pages;
    allocation.owner = owner;
    allocation.flags = flags;
    allocation.shared_references = (flags & resource::allocation_flag_shared) ? 1 : 0;
    allocation.mapping_count = 0;
    allocation.active = true;
    auto* bytes = reinterpret_cast<volatile uint64_t*>(boot_info_->memory.hhdm_offset + base);
    const uint64_t words = pages * page_size / sizeof(uint64_t);
    for (uint64_t i = 0; i < words; ++i) bytes[i] = 0;
    return {static_cast<uint32_t>(slot), allocation.generation};
}

bool PhysicalAllocator::physical_address(resource::AllocationHandle handle, uint64_t* address) const
{
    const Allocation* allocation = find(handle);
    if (allocation == nullptr || address == nullptr) return false;
    *address = allocation->base;
    return true;
}

bool PhysicalAllocator::allocation_info(resource::AllocationHandle handle,
    memory_module::AllocationInfo* info) const
{
    const Allocation* allocation = find(handle);
    if (allocation == nullptr || info == nullptr) return false;
    *info = {memory_module::allocation_info_version, handle, allocation->owner,
        allocation->base, allocation->pages, allocation->flags, allocation->shared_references};
    return true;
}

bool PhysicalAllocator::register_mapping(resource::AllocationHandle handle,
    uint64_t virtual_base, uint64_t physical_offset, uint64_t pages)
{
    Allocation* allocation = find(handle);
    if (allocation == nullptr || pages == 0 || allocation->mapping_count == maximum_mappings_per_allocation ||
        (virtual_base & 0xfff) != 0 || (physical_offset & 0xfff) != 0 ||
        physical_offset > allocation->pages * page_size ||
        pages > (allocation->pages * page_size - physical_offset) / page_size) return false;
    allocation->mappings[allocation->mapping_count++] = {virtual_base, physical_offset, pages};
    return true;
}

bool PhysicalAllocator::release(resource::AllocationHandle handle)
{
    Allocation* allocation = find(handle);
    if (allocation == nullptr || (allocation->flags & resource::allocation_flag_shared)) return false;
    return reclaim(*allocation);
}

bool PhysicalAllocator::destroy_owner_domain(resource::OwnerId owner)
{
    if (!initialized_ || owner.value == 0 || owner.kind == resource::OwnerKind::Kernel) return false;
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        const Allocation& allocation = allocations_[i];
        if (!allocation.active || !same_owner(allocation.owner, owner)) continue;
        if (allocation.flags & resource::allocation_flag_kernel_lifetime) return false;
        if ((allocation.flags & resource::allocation_flag_shared) && allocation.shared_references > 1)
            return false;
    }
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        Allocation& allocation = allocations_[i];
        if (allocation.active && same_owner(allocation.owner, owner) && !reclaim(allocation)) return false;
    }
    return true;
}

bool PhysicalAllocator::transfer_ownership(resource::AllocationHandle handle,
    resource::OwnerId new_owner)
{
    Allocation* allocation = find(handle);
    if (allocation == nullptr || new_owner.value == 0 || new_owner.generation == 0 ||
        new_owner.kind == resource::OwnerKind::Shared ||
        (allocation->flags & resource::allocation_flag_shared)) return false;
    allocation->owner = new_owner;
    return true;
}

bool PhysicalAllocator::transfer_owner(resource::OwnerId old_owner, resource::OwnerId new_owner)
{
    if (!initialized_ || new_owner.value == 0 || new_owner.generation == 0 ||
        old_owner.kind == resource::OwnerKind::Kernel || new_owner.kind == resource::OwnerKind::Shared) return false;
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        if (allocations_[i].active && same_owner(allocations_[i].owner, old_owner) &&
            (allocations_[i].flags & resource::allocation_flag_shared)) return false;
    }
    for (uint32_t i = 0; i < maximum_allocations; ++i)
        if (allocations_[i].active && same_owner(allocations_[i].owner, old_owner))
            allocations_[i].owner = new_owner;
    return true;
}

bool PhysicalAllocator::retain_shared(resource::AllocationHandle handle)
{
    Allocation* allocation = find(handle);
    if (allocation == nullptr || !(allocation->flags & resource::allocation_flag_shared) ||
        allocation->shared_references == UINT32_MAX) return false;
    ++allocation->shared_references;
    return true;
}

bool PhysicalAllocator::release_shared(resource::AllocationHandle handle)
{
    Allocation* allocation = find(handle);
    if (allocation == nullptr || !(allocation->flags & resource::allocation_flag_shared) ||
        allocation->shared_references == 0) return false;
    if (allocation->shared_references == 1) return reclaim(*allocation);
    --allocation->shared_references;
    return true;
}

bool PhysicalAllocator::query_owner(resource::OwnerId owner,
    memory_module::OwnerStats* stats) const
{
    if (!initialized_ || stats == nullptr || owner.value == 0) return false;
    *stats = {memory_module::owner_stats_version, owner, 0, 0, 0, 0};
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        const Allocation& allocation = allocations_[i];
        if (!allocation.active || !same_owner(allocation.owner, owner)) continue;
        ++stats->allocation_count;
        stats->total_pages += allocation.pages;
        stats->used_pages += allocation.pages;
    }
    return true;
}

uint64_t PhysicalAllocator::owner_count() const
{
    uint64_t count = 0;
    for (uint64_t i = 0; i < maximum_allocations; ++i) {
        const Allocation& allocation = allocations_[i];
        if (allocation.active && !seen_owner(allocation.owner, i)) ++count;
    }
    return count;
}

bool PhysicalAllocator::owner_at(uint64_t index, memory_module::OwnerStats* stats) const
{
    if (stats == nullptr) return false;
    uint64_t current = 0;
    for (uint64_t i = 0; i < maximum_allocations; ++i) {
        const Allocation& allocation = allocations_[i];
        if (!allocation.active || seen_owner(allocation.owner, i)) continue;
        if (current++ == index) return query_owner(allocation.owner, stats);
    }
    return false;
}

uint64_t PhysicalAllocator::allocations_for_owner(resource::OwnerId owner,
    uint64_t start_index, memory_module::AllocationInfo* output, uint64_t capacity) const
{
    uint64_t found = 0;
    uint64_t matched = 0;
    for (uint32_t i = 0; i < maximum_allocations; ++i) {
        const Allocation& allocation = allocations_[i];
        if (!allocation.active || !same_owner(allocation.owner, owner)) continue;
        if (output != nullptr && matched >= start_index && found < capacity) {
            output[found] = {memory_module::allocation_info_version,
                {i, allocation.generation}, allocation.owner, allocation.base,
                allocation.pages, allocation.flags, allocation.shared_references};
            ++found;
        }
        ++matched;
    }
    return found;
}

bool PhysicalAllocator::statistics(memory_module::Stats* stats) const
{
    if (!initialized_ || stats == nullptr) return false;
    *stats = {memory_module::stats_version, total_pages_, free_pages_,
        total_pages_ - free_pages_, owner_count()};
    return true;
}

bool PhysicalAllocator::remove_free_range(uint64_t base, uint64_t pages)
{
    for (uint32_t i = 0; i < free_extent_count_; ++i) {
        const uint64_t extent_base = free_extents_[i].base;
        const uint64_t extent_end = extent_base + free_extents_[i].pages * page_size;
        const uint64_t end = base + pages * page_size;
        if (base < extent_base || end > extent_end) continue;
        const uint64_t prefix = (base - extent_base) / page_size;
        const uint64_t suffix = (extent_end - end) / page_size;
        if (prefix != 0 && suffix != 0) {
            if (free_extent_count_ == maximum_free_extents) return false;
            for (uint32_t j = free_extent_count_; j > i + 1; --j) free_extents_[j] = free_extents_[j - 1];
            free_extents_[i] = {extent_base, prefix};
            free_extents_[i + 1] = {end, suffix};
            ++free_extent_count_;
        } else if (prefix != 0) free_extents_[i].pages = prefix;
        else if (suffix != 0) free_extents_[i] = {end, suffix};
        else {
            for (uint32_t j = i + 1; j < free_extent_count_; ++j) free_extents_[j - 1] = free_extents_[j];
            --free_extent_count_;
        }
        return true;
    }
    return false;
}

bool PhysicalAllocator::add_free_range(uint64_t base, uint64_t pages)
{
    if (pages == 0) return true;
    uint32_t index = 0;
    while (index < free_extent_count_ && free_extents_[index].base < base) ++index;
    if (free_extent_count_ == maximum_free_extents) return false;
    for (uint32_t i = free_extent_count_; i > index; --i) free_extents_[i] = free_extents_[i - 1];
    free_extents_[index] = {base, pages};
    ++free_extent_count_;
    for (uint32_t i = 0; i + 1 < free_extent_count_;) {
        const uint64_t end = free_extents_[i].base + free_extents_[i].pages * page_size;
        if (end < free_extents_[i + 1].base) { ++i; continue; }
        const uint64_t next_end = free_extents_[i + 1].base + free_extents_[i + 1].pages * page_size;
        if (next_end > end) free_extents_[i].pages = (next_end - free_extents_[i].base) / page_size;
        for (uint32_t j = i + 2; j < free_extent_count_; ++j) free_extents_[j - 1] = free_extents_[j];
        --free_extent_count_;
    }
    return true;
}

int32_t PhysicalAllocator::free_slot() const
{
    for (uint32_t i = 0; i < maximum_allocations; ++i)
        if (!allocations_[i].active) return static_cast<int32_t>(i);
    return -1;
}

PhysicalAllocator::Allocation* PhysicalAllocator::find(resource::AllocationHandle handle)
{
    if (handle.slot >= maximum_allocations) return nullptr;
    Allocation& allocation = allocations_[handle.slot];
    return allocation.active && allocation.generation == handle.generation ? &allocation : nullptr;
}

const PhysicalAllocator::Allocation* PhysicalAllocator::find(resource::AllocationHandle handle) const
{
    if (handle.slot >= maximum_allocations) return nullptr;
    const Allocation& allocation = allocations_[handle.slot];
    return allocation.active && allocation.generation == handle.generation ? &allocation : nullptr;
}

bool PhysicalAllocator::reclaim(Allocation& allocation)
{
    if (allocation.flags & resource::allocation_flag_kernel_lifetime) return false;
    for (uint32_t i = 0; i < allocation.mapping_count; ++i)
        if (!unmap(allocation.mappings[i], allocation.base)) return false;
    if (!add_free_range(allocation.base, allocation.pages)) return false;
    free_pages_ += allocation.pages;
    allocation.active = false;
    allocation.mapping_count = 0;
    return true;
}

bool PhysicalAllocator::unmap(const Mapping& mapping, uint64_t physical_base)
{
    for (uint64_t page = 0; page < mapping.pages; ++page) {
        const uint64_t virtual_address = mapping.virtual_base + page * page_size;
        uint64_t cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        auto* table = reinterpret_cast<uint64_t*>(boot_info_->memory.hhdm_offset + (cr3 & physical_mask));
        const uint16_t indexes[] = {
            static_cast<uint16_t>((virtual_address >> 39) & 0x1ff),
            static_cast<uint16_t>((virtual_address >> 30) & 0x1ff),
            static_cast<uint16_t>((virtual_address >> 21) & 0x1ff)
        };
        bool found = true;
        for (uint16_t index : indexes) {
            const uint64_t entry = table[index];
            if (!(entry & 1) || (entry & 0x80)) { found = false; break; }
            table = reinterpret_cast<uint64_t*>(boot_info_->memory.hhdm_offset + (entry & physical_mask));
        }
        if (!found) return false;
        uint64_t& leaf = table[(virtual_address >> 12) & 0x1ff];
        if (!(leaf & 1) || (leaf & physical_mask) !=
            physical_base + mapping.physical_offset + page * page_size) return false;
        leaf = 0;
        asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    }
    return true;
}

bool PhysicalAllocator::same_owner(resource::OwnerId left, resource::OwnerId right) const
{
    return left.value == right.value && left.generation == right.generation && left.kind == right.kind;
}

bool PhysicalAllocator::seen_owner(resource::OwnerId owner, uint64_t before_slot) const
{
    for (uint64_t i = 0; i < before_slot; ++i)
        if (allocations_[i].active && same_owner(allocations_[i].owner, owner)) return true;
    return false;
}

}
