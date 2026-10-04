#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include <rawline/boot_info.hpp>
#include <rawline/rwl.hpp>

namespace {

__attribute__((used, section(".limine_requests")))
volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
    .internal_module_count = 0,
    .internal_modules = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests_start")))
volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

rawline::boot::BootModule boot_modules[8];
rawline::boot::BootMemoryRange usable_ranges[256];
uint64_t usable_range_count;

[[noreturn]]
void halt()
{
    for (;;) {
        asm volatile("hlt");
    }
}

[[noreturn]]
void fail()
{
    for (;;) asm volatile("cli; hlt");
}

void serial_init()
{
    auto out = [](uint16_t port, uint8_t value) { asm volatile("outb %0, %w1" : : "a"(value), "d"(port)); };
    out(0x3f9, 0x00); out(0x3fb, 0x80); out(0x3f8, 0x01); out(0x3f9, 0x00);
    out(0x3fb, 0x03); out(0x3fa, 0xc7); out(0x3fc, 0x0b);
}

void serial_text(const char* text)
{
    while (*text) {
        uint8_t status;
        asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd}));
        if ((status & 0x20) == 0) continue;
        asm volatile("outb %0, %w1" : : "a"(*text++), "d"(uint16_t{0x3f8}));
    }
}

uint64_t* map_table(uint64_t physical_address)
{
    return reinterpret_cast<uint64_t*>(hhdm_request.response->offset + physical_address);
}

const char* file_name(const char* path)
{
    if (path == nullptr) return nullptr;
    const char* name = path;
    for (const char* current = path; *current != '\0'; ++current)
        if (*current == '/' || *current == '\\') name = current + 1;
    return name;
}

bool name_equals(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return false;
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == '\0' && *right == '\0';
}

uint64_t allocate_page(uint64_t& next_physical, uint64_t end_physical)
{
    if (next_physical > end_physical || end_physical - next_physical < 0x1000) return 0;
    const uint64_t result = next_physical;
    next_physical += 0x1000;
    auto* bytes = reinterpret_cast<volatile uint64_t*>(hhdm_request.response->offset + result);
    for (size_t i = 0; i < 0x1000 / sizeof(uint64_t); ++i) bytes[i] = 0;
    return result;
}

bool map_kernel_page(uint64_t virtual_address, uint64_t physical_address,
    bool writable, uint64_t& next_physical, uint64_t end_physical, bool uncached = false)
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    auto* pml4 = map_table(cr3 & ~uint64_t{0xfff});
    const uint16_t indexes[] = {
        static_cast<uint16_t>((virtual_address >> 39) & 0x1ff),
        static_cast<uint16_t>((virtual_address >> 30) & 0x1ff),
        static_cast<uint16_t>((virtual_address >> 21) & 0x1ff)
    };
    uint64_t* table = pml4;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t& entry = table[indexes[level]];
        if ((entry & 1) == 0) {
            const uint64_t child = allocate_page(next_physical, end_physical);
            if (child == 0) return false;
            entry = child | 0x3;
        } else if (level == 2 && (entry & 0x80)) {
            return false;
        }
        table = map_table(entry & ~uint64_t{0xfff});
    }
    const uint16_t page_index = static_cast<uint16_t>((virtual_address >> 12) & 0x1ff);
    uint64_t& page = table[page_index];
    if (page & 1) return false;
    page = (physical_address & ~uint64_t{0xfff}) | 1 | (writable ? 2 : 0) |
        (uncached ? 0x18 : 0);
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}

}

extern "C" [[noreturn]]
void rawline_main(const rawline::boot::BootInfo* boot);

extern "C" [[noreturn]]
void loader_main()
{
    serial_init();
    serial_text("RAWLINE loader entered\r\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_text("FAIL base revision\r\n");
        halt();
    }

    if (framebuffer_request.response == nullptr) {
        serial_text("FAIL framebuffer response\r\n");
        halt();
    }

    if (framebuffer_request.response->framebuffer_count == 0) {
        serial_text("FAIL framebuffer count\r\n");
        halt();
    }

    limine_framebuffer* framebuffer =
        framebuffer_request.response->framebuffers[0];

    if (framebuffer == nullptr) {
        halt();
    }

    if (module_request.response == nullptr) { serial_text("FAIL module response\r\n"); fail(); }
    if (module_request.response->module_count != 8 || module_request.response->module_count > 8) { serial_text("FAIL module count\r\n"); fail(); }
    if (module_request.response->modules == nullptr) { serial_text("FAIL module list\r\n"); fail(); }
    limine_file* kernel_module = nullptr;
    bool has_miraboot = false;
    bool has_memory = false;
    bool has_compositor = false;
    bool has_input = false;
    bool has_storage = false;
    bool has_filesystem = false;
    bool has_taskbar = false;
    for (uint64_t i = 0; i < module_request.response->module_count; ++i) {
        limine_file* file = module_request.response->modules[i];
        if (file == nullptr || file->address == nullptr || file->size < sizeof(rawline::rwl::Header)) {
            serial_text("FAIL module file\r\n"); fail();
        }
        const char* name = file_name(file->path);
        if (name == nullptr || *name == '\0') { serial_text("FAIL module path\r\n"); fail(); }
        boot_modules[i] = {
            .version = rawline::boot::boot_module_version,
            .id = i + 1,
            .generation = 1,
            .reserved = 0,
            .name = name,
            .address = file->address,
            .size = file->size
        };
        if (name_equals(name, "kernel.rwl")) kernel_module = file;
        if (name_equals(name, "miraboot.rwl")) has_miraboot = true;
        if (name_equals(name, "memory.rwl")) has_memory = true;
        if (name_equals(name, "compositor.rwl")) has_compositor = true;
        if (name_equals(name, "input.rwl")) has_input = true;
        if (name_equals(name, "storage.rwl")) has_storage = true;
        if (name_equals(name, "filesystem.rwl")) has_filesystem = true;
        if (name_equals(name, "taskbar.rwl")) has_taskbar = true;
    }
    if (kernel_module == nullptr || !has_miraboot || !has_memory || !has_compositor || !has_input ||
        !has_storage || !has_filesystem || !has_taskbar) { serial_text("FAIL required RWL modules\r\n"); fail(); }
    serial_text("loader has kernel, miraboot, memory, compositor, input, storage, filesystem and taskbar RWL modules\r\n");
    const auto* image = static_cast<const uint8_t*>(kernel_module->address);
    const auto* header = reinterpret_cast<const rawline::rwl::Header*>(image);
    if (header->magic != rawline::rwl::magic || header->version != rawline::rwl::version ||
        header->architecture != rawline::rwl::architecture_x86_64 ||
        header->flags != rawline::rwl::image_flag_relocatable ||
        header->preferred_virtual_base != 0xffffffffc0000000ull ||
        header->segment_count == 0 || header->segment_count > rawline::rwl::maximum_segments ||
        header->segments_offset != sizeof(rawline::rwl::Header) || header->image_size != kernel_module->size ||
        header->header_size != sizeof(rawline::rwl::Header) + header->segment_count * sizeof(rawline::rwl::Segment)) { serial_text("FAIL RWL header\r\n"); fail(); }
    const uint64_t table_size = uint64_t{header->segment_count} * sizeof(rawline::rwl::Segment);
    if (header->segments_offset > kernel_module->size || table_size > kernel_module->size - header->segments_offset) { serial_text("FAIL segment table\r\n"); fail(); }
    const auto* segments = reinterpret_cast<const rawline::rwl::Segment*>(image + header->segments_offset);
    if (memmap_request.response == nullptr || memmap_request.response->entries == nullptr) { serial_text("FAIL memmap response\r\n"); fail(); }
    usable_range_count = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; ++i) {
        const auto* map = memmap_request.response->entries[i];
        if (map == nullptr || map->base + map->length < map->base) { serial_text("FAIL memmap range\r\n"); fail(); }
        if (map->type != LIMINE_MEMMAP_USABLE || map->length == 0) continue;
        if (usable_range_count == 256) { serial_text("FAIL too many usable ranges\r\n"); fail(); }
        usable_ranges[usable_range_count++] = {map->base, map->length};
    }
    if (usable_range_count == 0) { serial_text("FAIL no usable memory ranges\r\n"); fail(); }
    bool entry_executable = false;
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        serial_text("copying kernel segment\r\n");
        if (segment.reserved != 0 || segment.memory_size == 0 || segment.file_size > segment.memory_size ||
            segment.file_offset > kernel_module->size || segment.file_size > kernel_module->size - segment.file_offset ||
            segment.image_offset + segment.memory_size < segment.image_offset ||
            segment.image_offset + segment.memory_size > 0x40000000ull || (segment.file_offset & 0xfff) ||
            (segment.flags & ~(rawline::rwl::segment_flag_read | rawline::rwl::segment_flag_write | rawline::rwl::segment_flag_execute)) ||
            !(segment.flags & rawline::rwl::segment_flag_read)) { serial_text("FAIL segment fields\r\n"); fail(); }
        for (uint16_t j = 0; j < i; ++j) {
            const auto& other = segments[j];
            if (segment.image_offset < other.image_offset + other.memory_size &&
                other.image_offset < segment.image_offset + segment.memory_size) { serial_text("FAIL overlapping segments\r\n"); fail(); }
        }
        bool available = false;
        for (uint64_t j = 0; j < memmap_request.response->entry_count; ++j) {
            const auto* map = memmap_request.response->entries[j];
            if (map == nullptr || map->base + map->length < map->base) fail();
            if (map->type == LIMINE_MEMMAP_USABLE && map->length >= segment.memory_size) available = true;
        }
        if (!available) { serial_text("FAIL segment not available\r\n"); fail(); }
        if (hhdm_request.response == nullptr) { serial_text("FAIL HHDM response\r\n"); fail(); }
        if (segment.image_offset > UINT64_MAX - 0xfff) fail();
        if (header->entry_address >= segment.image_offset &&
            header->entry_address - segment.image_offset < segment.file_size &&
            (segment.flags & rawline::rwl::segment_flag_execute)) entry_executable = true;
    }
    serial_text("kernel segment copied\r\n");
    if (!entry_executable) { serial_text("FAIL entry segment\r\n"); fail(); }
    serial_text("loader validated RWL\r\n");

    uint64_t image_span = 0;
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        if (segment.image_offset + segment.memory_size > image_span) image_span = segment.image_offset + segment.memory_size;
    }
    if (image_span > UINT64_MAX - 0x20000) fail();
    const uint64_t required_arena = image_span + 0x20000;
    bool allocation_found = false;
    uint64_t allocation_physical = 0;
    uint64_t allocation_end = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count && !allocation_found; ++i) {
        const auto* map = memmap_request.response->entries[i];
        if (map->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t start = map->base < 0x100000 ? 0x100000 : map->base;
        start = (start + 0xfff) & ~uint64_t{0xfff};
        if (start >= map->base && map->length >= start - map->base &&
            required_arena <= map->length - (start - map->base) && map->base + map->length >= map->base) {
            allocation_physical = start;
            allocation_end = map->base + map->length;
            allocation_found = true;
        }
    }
    if (!allocation_found || allocation_physical > UINT64_MAX - hhdm_request.response->offset) fail();
    auto* allocation = reinterpret_cast<uint8_t*>(hhdm_request.response->offset + allocation_physical);
    for (uint64_t i = 0; i < image_span; ++i) allocation[i] = 0;
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        auto* destination = allocation + segment.image_offset;
        const auto* source = image + segment.file_offset;
        for (uint64_t j = 0; j < segment.file_size; ++j) destination[j] = source[j];
        for (uint64_t j = segment.file_size; j < segment.memory_size; ++j) destination[j] = 0;
    }
    serial_text("RWL image copied; installing linked virtual mappings\r\n");
    uint64_t allocation_end_cursor =
        (allocation_physical + image_span + 0xfff) & ~uint64_t{0xfff};
    for (uint16_t i = 0; i < header->segment_count; ++i) {
        const auto& segment = segments[i];
        const uint64_t first_page = segment.image_offset & ~uint64_t{0xfff};
        const uint64_t end_page = (segment.image_offset + segment.memory_size + 0xfff) & ~uint64_t{0xfff};
        for (uint64_t offset = first_page; offset < end_page; offset += 0x1000) {
            if (!map_kernel_page(header->preferred_virtual_base + offset,
                    allocation_physical + offset,
                    (segment.flags & rawline::rwl::segment_flag_write) != 0,
                    allocation_end_cursor, allocation_end)) {
                serial_text("FAIL mapping kernel page\r\n"); fail();
            }
        }
    }
    uint32_t apic_base_low;
    uint32_t apic_base_high;
    asm volatile("rdmsr" : "=a"(apic_base_low), "=d"(apic_base_high) : "c"(uint32_t{0x1b}));
    const uint64_t apic_physical =
        ((uint64_t{apic_base_high} << 32) | apic_base_low) & 0x0000000ffffff000ull;
    if (apic_physical == 0 || !map_kernel_page(0xffffffffd0000000ull, apic_physical,
            true, allocation_end_cursor, allocation_end, true)) {
        serial_text("FAIL mapping local APIC MMIO\r\n"); fail();
    }

    if (framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        serial_text("FAIL framebuffer model\r\n");
        halt();
    }

    if (framebuffer->bpp != 32) {
        serial_text("FAIL framebuffer bpp\r\n");
        halt();
    }

    rawline::boot::BootInfo boot = {
        .version = rawline::boot::boot_info_version,

        .framebuffer = {
            .address = reinterpret_cast<uint64_t>(
                framebuffer->address
            ),

            .width = framebuffer->width,
            .height = framebuffer->height,
            .pitch = framebuffer->pitch,

            .bits_per_pixel = framebuffer->bpp,

            .red_mask_size = framebuffer->red_mask_size,
            .red_mask_shift = framebuffer->red_mask_shift,

            .green_mask_size = framebuffer->green_mask_size,
            .green_mask_shift = framebuffer->green_mask_shift,

            .blue_mask_size = framebuffer->blue_mask_size,
            .blue_mask_shift = framebuffer->blue_mask_shift
        },
        .modules = {
            .version = rawline::boot::boot_modules_version,
            .count = module_request.response->module_count,
            .entries = boot_modules
        },
        .memory = {
            .version = rawline::boot::boot_memory_version,
            .hhdm_offset = hhdm_request.response->offset,
            .base_physical = allocation_physical,
            .next_physical = allocation_end_cursor,
            .end_physical = allocation_end
        },
        .memory_map = {
            .version = rawline::boot::boot_memory_map_version,
            .count = usable_range_count,
            .usable_ranges = usable_ranges
        }
    };

    serial_text("transferring to RWL kernel\r\n");
    using KernelEntry = void (*)(const rawline::boot::BootInfo*);
    reinterpret_cast<KernelEntry>(header->preferred_virtual_base + header->entry_address)(&boot);
    fail();
}
