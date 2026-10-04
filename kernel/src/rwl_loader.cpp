#include <stddef.h>
#include <stdint.h>

#include <rawline/kernel/rwl_loader.hpp>
#include <rawline/kernel/memory/physical_allocator.hpp>
#include <rawline/rwl.hpp>

namespace {

constexpr uint64_t page_size = 0x1000;
constexpr uint64_t minimum_module_base = 0xffffffffe0000000ull;
constexpr uint64_t maximum_module_base = 0xfffffffff0000000ull;
constexpr uint64_t module_window_size = 0x10000000ull;
constexpr uint64_t physical_mask = 0x000ffffffffff000ull;
extern "C" int64_t rawline_invoke_rwl(int64_t (*entry)(const void*), const void* context);

void serial_text(const char* text)
{
    while (*text) {
        uint8_t status;
        asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd}));
        if ((status & 0x20) == 0) continue;
        asm volatile("outb %0, %w1" : : "a"(*text++), "d"(uint16_t{0x3f8}));
    }
}

bool equal(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return false;
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == '\0' && *right == '\0';
}

bool add_overflows(uint64_t left, uint64_t right)
{
    return left > UINT64_MAX - right;
}

uint64_t align_up(uint64_t value)
{
    return (value + page_size - 1) & ~(page_size - 1);
}

uint64_t* hhdm_pointer(uint64_t offset, uint64_t physical)
{
    return reinterpret_cast<uint64_t*>(offset + physical);
}

uint64_t allocate_page(rawline::boot::BootInfo* boot_info)
{
    constexpr rawline::resource::OwnerId kernel_owner{1, 1, rawline::resource::OwnerKind::Kernel};
    auto& allocator = rawline::kernel::memory::physical_allocator();
    const auto handle = allocator.allocate_pages(kernel_owner, 1,
        rawline::resource::allocation_flag_kernel_lifetime);
    uint64_t page;
    if (!allocator.physical_address(handle, &page) ||
        add_overflows(boot_info->memory.hhdm_offset, page)) return 0;
    return page;
}

bool map_page(uint64_t virtual_address, uint64_t physical_address, bool writable,
    rawline::boot::BootInfo* boot_info)
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    auto* table = hhdm_pointer(boot_info->memory.hhdm_offset, cr3 & physical_mask);
    const uint16_t indexes[] = {
        static_cast<uint16_t>((virtual_address >> 39) & 0x1ff),
        static_cast<uint16_t>((virtual_address >> 30) & 0x1ff),
        static_cast<uint16_t>((virtual_address >> 21) & 0x1ff)
    };
    for (size_t level = 0; level < 3; ++level) {
        uint64_t& entry = table[indexes[level]];
        if ((entry & 1) == 0) {
            const uint64_t child = allocate_page(boot_info);
            if (child == 0) return false;
            entry = child | 3;
        } else {
            if (level == 2 && (entry & 0x80)) return false;
            entry |= 2;
        }
        table = hhdm_pointer(boot_info->memory.hhdm_offset, entry & physical_mask);
    }
    uint64_t& leaf = table[(virtual_address >> 12) & 0x1ff];
    if (leaf & 1) return false;
    leaf = (physical_address & physical_mask) | 1 | (writable ? 2 : 0);
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}

bool valid_boot_info(const rawline::boot::BootInfo* boot_info)
{
    return boot_info != nullptr && boot_info->version == rawline::boot::boot_info_version &&
        boot_info->memory.version == rawline::boot::boot_memory_version &&
        boot_info->memory_map.version == rawline::boot::boot_memory_map_version;
}

bool get_owner_stats_callback(const rawline::boot::BootInfo* boot_info, uint64_t index,
    rawline::memory_module::OwnerStats* stats)
{
    return valid_boot_info(boot_info) &&
        rawline::kernel::memory::physical_allocator().owner_at(index, stats);
}

uint64_t get_owner_allocations_callback(const rawline::boot::BootInfo* boot_info,
    rawline::resource::OwnerId owner, uint64_t start_index,
    rawline::memory_module::AllocationInfo* output, uint64_t capacity)
{
    if (!valid_boot_info(boot_info)) return 0;
    return rawline::kernel::memory::physical_allocator().allocations_for_owner(
        owner, start_index, output, capacity);
}

rawline::resource::ResourceDomain create_domain_callback(rawline::resource::OwnerKind kind)
{
    return rawline::kernel::memory::physical_allocator().create_domain(kind);
}

rawline::resource::AllocationHandle allocate_pages_callback(rawline::resource::OwnerId owner,
    uint64_t pages, uint32_t flags)
{
    return rawline::kernel::memory::physical_allocator().allocate_pages(owner, pages, flags);
}

bool destroy_domain_callback(rawline::resource::OwnerId owner)
{
    return rawline::kernel::memory::physical_allocator().destroy_owner_domain(owner);
}

bool release_allocation_callback(rawline::resource::AllocationHandle handle)
{
    return rawline::kernel::memory::physical_allocator().release(handle);
}

bool transfer_owner_callback(rawline::resource::OwnerId from, rawline::resource::OwnerId to)
{
    return rawline::kernel::memory::physical_allocator().transfer_owner(from, to);
}

bool retain_shared_callback(rawline::resource::AllocationHandle handle)
{
    return rawline::kernel::memory::physical_allocator().retain_shared(handle);
}

bool release_shared_callback(rawline::resource::AllocationHandle handle)
{
    return rawline::kernel::memory::physical_allocator().release_shared(handle);
}

bool valid_segment(const rawline::rwl::Segment& segment, uint64_t file_size)
{
    if (segment.reserved != 0 || segment.memory_size == 0 ||
        segment.file_size > segment.memory_size ||
        segment.file_offset > file_size || segment.file_size > file_size - segment.file_offset ||
        segment.image_offset >= module_window_size ||
        segment.memory_size > module_window_size - segment.image_offset ||
        (segment.file_offset & (page_size - 1)) != 0 ||
        (segment.image_offset & (page_size - 1)) != 0 ||
        (segment.flags & ~(rawline::rwl::segment_flag_read | rawline::rwl::segment_flag_write |
            rawline::rwl::segment_flag_execute)) != 0 ||
        (segment.flags & rawline::rwl::segment_flag_read) == 0) return false;
    return true;
}

}

const rawline::boot::BootModule* rawline::kernel::find_boot_module(
    const boot::BootInfo* boot_info, const char* name)
{
    if (boot_info == nullptr || name == nullptr ||
        boot_info->version != boot::boot_info_version ||
        boot_info->modules.version != boot::boot_modules_version ||
        boot_info->modules.entries == nullptr) return nullptr;
    for (uint64_t i = 0; i < boot_info->modules.count; ++i) {
        const auto& candidate = boot_info->modules.entries[i];
        if (candidate.version != boot::boot_module_version || candidate.name == nullptr ||
            candidate.address == nullptr) return nullptr;
        if (equal(candidate.name, name)) return &candidate;
    }
    return nullptr;
}

bool rawline::kernel::get_memory_stats(const boot::BootInfo* boot_info,
    memory_module::Stats* stats)
{
    return valid_boot_info(boot_info) &&
        memory::physical_allocator().statistics(stats);
}

bool rawline::kernel::get_memory_owner_stats(const boot::BootInfo* boot_info,
    uint64_t index, memory_module::OwnerStats* stats)
{
    return get_owner_stats_callback(boot_info, index, stats);
}

uint64_t rawline::kernel::get_memory_owner_allocations(const boot::BootInfo* boot_info,
    resource::OwnerId owner, uint64_t start_index,
    memory_module::AllocationInfo* output, uint64_t capacity)
{
    return get_owner_allocations_callback(boot_info, owner, start_index, output, capacity);
}

rawline::resource::ResourceDomain rawline::kernel::create_memory_domain(rawline::resource::OwnerKind kind)
{
    return create_domain_callback(kind);
}

rawline::resource::AllocationHandle rawline::kernel::allocate_memory_pages(rawline::resource::OwnerId owner,
    uint64_t pages, uint32_t flags)
{
    return allocate_pages_callback(owner, pages, flags);
}

bool rawline::kernel::release_memory_allocation(rawline::resource::AllocationHandle handle)
{
    return release_allocation_callback(handle);
}

bool rawline::kernel::destroy_memory_domain(resource::OwnerId owner)
{
    return destroy_domain_callback(owner);
}

bool rawline::kernel::transfer_memory_owner(resource::OwnerId from, resource::OwnerId to)
{
    return transfer_owner_callback(from, to);
}

bool rawline::kernel::retain_shared_memory(resource::AllocationHandle handle)
{
    return retain_shared_callback(handle);
}

bool rawline::kernel::release_shared_memory(resource::AllocationHandle handle)
{
    return release_shared_callback(handle);
}

int64_t rawline::kernel::start_rwl_module(const boot::BootInfo* boot_info,
    const boot::BootModule* module, const void* context)
{
    if (boot_info == nullptr || module == nullptr || context == nullptr ||
        boot_info->version != boot::boot_info_version ||
        boot_info->memory.version != boot::boot_memory_version ||
        boot_info->memory.hhdm_offset == 0) return -1;
    bool is_listed = false;
    for (uint64_t i = 0; i < boot_info->modules.count; ++i)
        if (&boot_info->modules.entries[i] == module) is_listed = true;
    if (!is_listed || module->version != boot::boot_module_version ||
        module->id == 0 || module->generation == 0 ||
        module->address == nullptr || module->size < sizeof(rwl::Header)) return -1;

    auto& allocator = memory::physical_allocator();
    const resource::OwnerId owner{module->id, module->generation, resource::OwnerKind::Module};

    const auto* image = static_cast<const uint8_t*>(module->address);
    const auto* header = reinterpret_cast<const rwl::Header*>(image);
    if (header->magic != rwl::magic || header->version != rwl::version ||
        header->architecture != rwl::architecture_x86_64 ||
        header->flags != rwl::image_flag_relocatable ||
        header->preferred_virtual_base < minimum_module_base ||
        header->preferred_virtual_base >= maximum_module_base ||
        header->segment_count == 0 || header->segment_count > rwl::maximum_segments ||
        header->segments_offset != sizeof(rwl::Header) || header->image_size != module->size ||
        header->segments_offset > module->size ||
        uint64_t{header->segment_count} * sizeof(rwl::Segment) > module->size - header->segments_offset ||
        header->header_size != sizeof(rwl::Header) + header->segment_count * sizeof(rwl::Segment)) return -1;

    const auto* segments = reinterpret_cast<const rwl::Segment*>(image + header->segments_offset);
    uint64_t image_span = 0;
    bool entry_executable = false;
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        if (!valid_segment(segment, module->size)) return -1;
        const uint64_t end = segment.image_offset + segment.memory_size;
        if (end > image_span) image_span = end;
        if (header->entry_address >= segment.image_offset &&
            header->entry_address - segment.image_offset < segment.file_size &&
            (segment.flags & rwl::segment_flag_execute)) entry_executable = true;
        for (uint16_t j = 0; j < i; ++j) {
            const auto& other = segments[j];
            if (segment.image_offset < other.image_offset + other.memory_size &&
                other.image_offset < end) return -1;
        }
    }
    if (!entry_executable || image_span == 0 || image_span > module_window_size ||
        image_span > maximum_module_base - header->preferred_virtual_base ||
        header->entry_address >= module_window_size) return -1;

    if (!allocator.destroy_owner_domain(owner)) return -1;
    const uint64_t pages = align_up(image_span) / page_size;
    const auto handle = allocator.allocate_pages(owner, pages);
    uint64_t image_physical;
    if (!allocator.physical_address(handle, &image_physical) ||
        add_overflows(boot_info->memory.hhdm_offset, image_physical)) {
        allocator.destroy_owner_domain(owner);
        return -1;
    }
    auto* destination = reinterpret_cast<uint8_t*>(boot_info->memory.hhdm_offset + image_physical);
    for (uint64_t i = 0; i < pages * page_size; ++i) destination[i] = 0;
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        const auto* source = image + segment.file_offset;
        auto* target = destination + segment.image_offset;
        for (uint64_t j = 0; j < segment.file_size; ++j) target[j] = source[j];
    }

    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        const uint64_t end = align_up(segment.image_offset + segment.memory_size);
        uint64_t mapped_pages = 0;
        for (uint64_t offset = segment.image_offset; offset < end; offset += page_size) {
            if (!map_page(header->preferred_virtual_base + offset, image_physical + offset,
                    (segment.flags & rwl::segment_flag_write) != 0,
                    const_cast<boot::BootInfo*>(boot_info))) {
                if (mapped_pages != 0 && !allocator.register_mapping(handle,
                        header->preferred_virtual_base + segment.image_offset,
                        segment.image_offset, mapped_pages)) return -1;
                allocator.destroy_owner_domain(owner);
                return -1;
            }
            ++mapped_pages;
        }
        if (!allocator.register_mapping(handle, header->preferred_virtual_base + segment.image_offset,
                segment.image_offset, mapped_pages)) {
            allocator.destroy_owner_domain(owner);
            return -1;
        }
    }

    serial_text("kernel: RWL module validated and mapped\r\n");
    using EntryPoint = int64_t (*)(const void* context);
    const auto entry = reinterpret_cast<EntryPoint>(header->preferred_virtual_base + header->entry_address);
    const int64_t result = rawline_invoke_rwl(entry, context);
    if (result != 0) allocator.destroy_owner_domain(owner);
    return result;
}
