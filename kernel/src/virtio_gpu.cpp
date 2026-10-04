#include <stdint.h>

#include <rawline/kernel/graphics.hpp>
#include <rawline/kernel/memory/physical_allocator.hpp>
#include <rawline/kernel/display.hpp>

namespace {

constexpr uint16_t pci_address_port = 0xcf8;
constexpr uint16_t pci_data_port = 0xcfc;
constexpr uint64_t page_size = 4096;
constexpr uint16_t maximum_queue_size = 256;
constexpr uint64_t queue_allocation_pages = 8;
constexpr uint64_t command_slot_bytes = 128;
constexpr uint32_t resource_id = 1;
constexpr rawline::resource::OwnerId kernel_owner{1, 1, rawline::resource::OwnerKind::Kernel};

struct Descriptor { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; } __attribute__((packed));
struct UsedElement { uint32_t id; uint32_t length; } __attribute__((packed));
struct Rect { uint32_t x, y, width, height; } __attribute__((packed));
struct Header { uint32_t type, flags; uint64_t fence_id; uint32_t context_id, padding; } __attribute__((packed));
struct Create2d { Header header; uint32_t resource, width, height, format; } __attribute__((packed));
struct AttachBacking { Header header; uint32_t resource, entries; uint64_t address; uint32_t length, padding; } __attribute__((packed));
struct SetScanout { Header header; Rect rect; uint32_t scanout, resource; } __attribute__((packed));
struct Transfer2d { Header header; Rect rect; uint64_t offset; uint32_t resource, padding; } __attribute__((packed));
struct Flush { Header header; Rect rect; uint32_t resource, padding; } __attribute__((packed));
struct DisplayEntry { Rect rect; uint32_t enabled, flags; } __attribute__((packed));
struct DisplayInfo { Header header; DisplayEntry entries[16]; } __attribute__((packed));

volatile uint8_t* common_cfg;
volatile uint16_t* queue_notify;
uint64_t hhdm_offset;
uint64_t queue_physical;
uint8_t* queue_base;
Descriptor* descriptors;
volatile uint16_t* available_index;
uint16_t* available_entries;
volatile uint16_t* used_index;
uint16_t queue_size;
uint16_t last_used;
uint64_t queue_available_physical;
uint64_t queue_used_physical;
uint64_t command_slot_offset;
uint64_t command_slot_capacity;
uint64_t notify_multiplier;
uint64_t notify_region_length;
uint64_t framebuffer_physical;
uint64_t framebuffer_pages;
uint32_t* framebuffer;
uint32_t display_width;
uint32_t display_height;
bool ready;
uint64_t next_mmio_virtual = 0xfffffe0000000000ull;

void serial_text_digit(char value);

void out32(uint16_t port, uint32_t value) { asm volatile("outl %0, %w1" : : "a"(value), "d"(port)); }
uint32_t in32(uint16_t port) { uint32_t value; asm volatile("inl %w1, %0" : "=a"(value) : "d"(port)); return value; }

uint32_t mmio_read32(volatile uint8_t* base, uint32_t offset)
{
    return *reinterpret_cast<volatile uint32_t*>(base + offset);
}

void mmio_write32(volatile uint8_t* base, uint32_t offset, uint32_t value)
{
    *reinterpret_cast<volatile uint32_t*>(base + offset) = value;
}

void mmio_write16(volatile uint8_t* base, uint32_t offset, uint16_t value)
{
    *reinterpret_cast<volatile uint16_t*>(base + offset) = value;
}

void serial_text(const char* text)
{
    while (*text) {
        uint8_t status;
        asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd}));
        if (status & 0x20) asm volatile("outb %0, %w1" : : "a"(*text++), "d"(uint16_t{0x3f8}));
    }
}

void serial_hex(uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) serial_text_digit(digits[(value >> shift) & 0xf]);
}

void serial_text_digit(char value)
{
    uint8_t status;
    do { asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd})); } while ((status & 0x20) == 0);
    asm volatile("outb %0, %w1" : : "a"(value), "d"(uint16_t{0x3f8}));
}

uint32_t pci_read(uint8_t slot, uint8_t offset)
{
    out32(pci_address_port, 0x80000000u | (uint32_t{slot} << 11) | (offset & 0xfcu));
    return in32(pci_data_port);
}

void pci_write(uint8_t slot, uint8_t offset, uint32_t value)
{
    out32(pci_address_port, 0x80000000u | (uint32_t{slot} << 11) | (offset & 0xfcu));
    out32(pci_data_port, value);
}

uint64_t map_mmio(uint64_t physical, uint64_t bytes)
{
    if (bytes == 0 || physical > UINT64_MAX - bytes) return 0;
    const uint64_t page_physical = physical & ~(page_size - 1);
    const uint64_t displacement = physical - page_physical;
    const uint64_t pages = (displacement + bytes + page_size - 1) / page_size;
    const uint64_t mapped_bytes = pages * page_size;
    const uint64_t virtual_base = next_mmio_virtual;
    if (mapped_bytes > UINT64_MAX - virtual_base) return 0;
    auto& allocator = rawline::kernel::memory::physical_allocator();
    for (uint64_t page = 0; page < pages; ++page) {
        const uint64_t virtual_address = virtual_base + page * page_size;
        uint64_t cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        auto* table = reinterpret_cast<uint64_t*>(hhdm_offset + (cr3 & ~uint64_t{0xfff}));
        const uint16_t indexes[]{static_cast<uint16_t>((virtual_address >> 39) & 0x1ff),
            static_cast<uint16_t>((virtual_address >> 30) & 0x1ff),
            static_cast<uint16_t>((virtual_address >> 21) & 0x1ff)};
        for (uint16_t level = 0; level < 3; ++level) {
            uint64_t& entry = table[indexes[level]];
            if ((entry & 1) == 0) {
                const auto allocation = allocator.allocate_pages(kernel_owner, 1,
                    rawline::resource::allocation_flag_kernel_lifetime);
                uint64_t child;
                if (allocation.generation == 0 || !allocator.physical_address(allocation, &child)) return 0;
                entry = child | 3;
            } else if (entry & 0x80) {
                return 0;
            }
            table = reinterpret_cast<uint64_t*>(hhdm_offset + (entry & ~uint64_t{0xfff}));
        }
        uint64_t& leaf = table[(virtual_address >> 12) & 0x1ff];
        if (leaf & 1) return 0;
        leaf = (page_physical + page * page_size) | 0x800000000000001bull; // present, writable, PWT, PCD, NX
        asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    }
    next_mmio_virtual += mapped_bytes + page_size;
    return virtual_base + displacement;
}

uint64_t pci_bar_address(uint8_t slot, uint8_t bar_index)
{
    if (bar_index >= 6) return 0;
    const uint32_t low = pci_read(slot, static_cast<uint8_t>(0x10 + bar_index * 4));
    if ((low & 1) != 0) return 0;
    const uint32_t type = (low >> 1) & 3;
    uint64_t address = low & ~uint32_t{0xf};
    if (type == 2 && bar_index < 5)
        address |= uint64_t{pci_read(slot, static_cast<uint8_t>(0x14 + bar_index * 4))} << 32;
    return address;
}

bool find_virtio_capability(uint8_t slot, uint8_t type, uint64_t* mapped,
    uint64_t* region_offset, uint64_t* region_length, uint32_t* notify_multiplier)
{
    const uint8_t status = static_cast<uint8_t>((pci_read(slot, 0x04) >> 16) & 0xff);
    if ((status & 0x10) == 0) return false;
    uint8_t capability = static_cast<uint8_t>(pci_read(slot, 0x34) & 0xfc);
    for (uint32_t count = 0; capability >= 0x40 && count < 48; ++count) {
        const uint32_t header = pci_read(slot, capability);
        const uint8_t id = static_cast<uint8_t>(header & 0xff);
        const uint8_t next = static_cast<uint8_t>((header >> 8) & 0xff);
        const uint8_t length = static_cast<uint8_t>((header >> 16) & 0xff);
        const uint8_t cfg_type = static_cast<uint8_t>((header >> 24) & 0xff);
        if (id == 0x09 && length >= 16 && cfg_type == type) {
            const uint32_t bar_info = pci_read(slot, static_cast<uint8_t>(capability + 4));
            const uint8_t bar = static_cast<uint8_t>(bar_info & 0xff);
            const uint32_t offset = pci_read(slot, static_cast<uint8_t>(capability + 8));
            const uint32_t bytes = pci_read(slot, static_cast<uint8_t>(capability + 12));
            uint32_t multiplier = 0;
            if (type == 2 && length >= 20)
                multiplier = pci_read(slot, static_cast<uint8_t>(capability + 16));
            const uint64_t bar_base = pci_bar_address(slot, bar);
            if (bar_base == 0 || bytes == 0 || offset > UINT64_MAX - bar_base) return false;
            *mapped = map_mmio(bar_base + offset, bytes);
            if (*mapped == 0) return false;
            *region_offset = offset;
            *region_length = bytes;
            if (notify_multiplier != nullptr) *notify_multiplier = multiplier;
            return true;
        }
        if (next == 0 || next == capability) break;
        capability = static_cast<uint8_t>(next & 0xfc);
    }
    return false;
}

uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

bool command(const void* request, uint32_t request_bytes, void* response,
    uint32_t response_bytes, uint32_t expected_response)
{
    if (!ready || request == nullptr || response == nullptr || request_bytes > 512 || response_bytes > 1024)
        return false;
    auto* req = queue_base + 3 * page_size;
    auto* resp = req + 512;
    auto* source = static_cast<const uint8_t*>(request);
    for (uint32_t i = 0; i < request_bytes; ++i) req[i] = source[i];
    for (uint32_t i = 0; i < response_bytes; ++i) resp[i] = 0;
    descriptors[0] = {queue_physical + 3 * page_size, request_bytes, 1, 1};
    descriptors[1] = {queue_physical + 3 * page_size + 512, response_bytes, 2, 0};
    available_entries[*available_index % queue_size] = 0;
    asm volatile("mfence" : : : "memory");
    *available_index = static_cast<uint16_t>(*available_index + 1);
    mmio_write16(reinterpret_cast<volatile uint8_t*>(queue_notify), 0, 0);
    uint64_t spins = 0;
    while (*used_index == last_used && spins++ < 100000000) asm volatile("pause");
    if (*used_index == last_used) return false;
    ++last_used;
    asm volatile("lfence" : : : "memory");
    auto* destination = static_cast<uint8_t*>(response);
    for (uint32_t i = 0; i < response_bytes; ++i) destination[i] = resp[i];
    const auto* header = static_cast<const Header*>(response);
    if (header->type != expected_response) {
        serial_text("graphics: VirtIO-GPU response type="); serial_hex(header->type);
        serial_text(" expected="); serial_hex(expected_response); serial_text("\r\n");
    }
    return header->type == expected_response;
}

bool command_no_data(const void* request, uint32_t request_bytes)
{
    Header response{};
    return command(request, request_bytes, &response, sizeof(response), 0x1100);
}

bool initialize_queue()
{
    // Modern VirtIO feature negotiation: 2D requires no optional features,
    // while modern PCI transport requires VERSION_1.
    common_cfg[20] = 0;
    common_cfg[20] = 1;
    common_cfg[20] = 3;
    mmio_write32(common_cfg, 0, 0);
    const uint32_t host_features_low = mmio_read32(common_cfg, 4);
    mmio_write32(common_cfg, 8, 0);
    mmio_write32(common_cfg, 12, 0);
    mmio_write32(common_cfg, 0, 1);
    const uint32_t host_features_high = mmio_read32(common_cfg, 4);
    if ((host_features_high & 1) == 0) return false;
    mmio_write32(common_cfg, 8, 1);
    mmio_write32(common_cfg, 12, 1);
    common_cfg[20] = 11;
    if ((common_cfg[20] & 8) == 0) return false;
    (void)host_features_low;
    mmio_write16(common_cfg, 22, 0);
    queue_size = *reinterpret_cast<volatile uint16_t*>(common_cfg + 24);
    if (queue_size == 0 || queue_size > maximum_queue_size) return false;
    auto& allocator = rawline::kernel::memory::physical_allocator();
    const auto allocation = allocator.allocate_pages(kernel_owner, queue_allocation_pages,
        rawline::resource::allocation_flag_kernel_lifetime);
    if (allocation.generation == 0 || !allocator.physical_address(allocation, &queue_physical) ||
        hhdm_offset > UINT64_MAX - queue_physical) return false;
    queue_base = reinterpret_cast<uint8_t*>(hhdm_offset + queue_physical);
    for (uint64_t i = 0; i < queue_allocation_pages * page_size; ++i) queue_base[i] = 0;
    descriptors = reinterpret_cast<Descriptor*>(queue_base);
    auto* available = queue_base + uint64_t{queue_size} * sizeof(Descriptor);
    available_index = reinterpret_cast<volatile uint16_t*>(available + 2);
    available_entries = reinterpret_cast<uint16_t*>(available + 4);
    queue_available_physical = queue_physical + uint64_t{queue_size} * sizeof(Descriptor);
    const uint64_t used_offset = align_up(uint64_t{queue_size} * sizeof(Descriptor) + 4 + uint64_t{queue_size} * 2 + 2, 4);
    used_index = reinterpret_cast<volatile uint16_t*>(queue_base + used_offset + 2);
    queue_used_physical = queue_physical + used_offset;
    command_slot_offset = align_up(used_offset + 4 + uint64_t{queue_size} * sizeof(UsedElement) + 2, page_size);
    command_slot_capacity = (queue_allocation_pages * page_size - command_slot_offset) / command_slot_bytes;
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 22) = 0;
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 24) = queue_size;
    mmio_write32(common_cfg, 32, static_cast<uint32_t>(queue_physical));
    mmio_write32(common_cfg, 36, static_cast<uint32_t>(queue_physical >> 32));
    mmio_write32(common_cfg, 40, static_cast<uint32_t>(queue_available_physical));
    mmio_write32(common_cfg, 44, static_cast<uint32_t>(queue_available_physical >> 32));
    mmio_write32(common_cfg, 48, static_cast<uint32_t>(queue_used_physical));
    mmio_write32(common_cfg, 52, static_cast<uint32_t>(queue_used_physical >> 32));
    *reinterpret_cast<volatile uint16_t*>(common_cfg + 28) = 1;
    const uint16_t notify_offset = *reinterpret_cast<volatile uint16_t*>(common_cfg + 30);
    const uint64_t byte_offset = uint64_t{notify_offset} * notify_multiplier;
    if (byte_offset + 2 > notify_region_length) return false;
    queue_notify = reinterpret_cast<volatile uint16_t*>(reinterpret_cast<volatile uint8_t*>(queue_notify) + byte_offset);
    return true;
}

bool setup_scanout(const rawline::boot::BootInfo* boot)
{
    DisplayInfo display{};
    Header request{0x0100, 0, 0, 0, 0};
    if (!command(&request, sizeof(request), &display, sizeof(display), 0x1101) ||
        display.header.type != 0x1101) {
        serial_text("graphics: VirtIO-GPU GET_DISPLAY_INFO failed\r\n");
        return false;
    }
    serial_text("graphics: VirtIO-GPU scanout0 enabled="); serial_hex(display.entries[0].enabled);
    serial_text(" size="); serial_hex(display.entries[0].rect.width); serial_text("x");
    serial_hex(display.entries[0].rect.height); serial_text(" firmware=");
    serial_hex(boot->framebuffer.width); serial_text("x"); serial_hex(boot->framebuffer.height); serial_text("\r\n");
    display_width = static_cast<uint32_t>(boot->framebuffer.width);
    display_height = static_cast<uint32_t>(boot->framebuffer.height);
    if (display_width == 0 || display_height == 0) return false;
    const uint64_t bytes = uint64_t{display_width} * display_height * 4;
    if (bytes / 4 / display_width != display_height) return false;
    framebuffer_pages = (bytes + page_size - 1) / page_size;
    auto& allocator = rawline::kernel::memory::physical_allocator();
    const auto allocation = allocator.allocate_pages(kernel_owner, framebuffer_pages,
        rawline::resource::allocation_flag_kernel_lifetime);
    if (allocation.generation == 0 || !allocator.physical_address(allocation, &framebuffer_physical) ||
        framebuffer_physical >= 0x100000000ull || bytes > UINT32_MAX ||
        hhdm_offset > UINT64_MAX - framebuffer_physical) return false;
    framebuffer = reinterpret_cast<uint32_t*>(hhdm_offset + framebuffer_physical);
    for (uint64_t i = 0; i < bytes / 4; ++i) framebuffer[i] = 0;

    // A8 is used because it is the most widely supported VirtIO-GPU 2D
    // format; presentation forces opaque alpha from the compositor's RGBX.
    Create2d create{{0x0101, 0, 0, 0, 0}, resource_id, 1, display_width, display_height};
    if (!command_no_data(&create, sizeof(create))) { serial_text("graphics: VirtIO-GPU CREATE_2D failed\r\n"); return false; }
    AttachBacking attach{{0x0106, 0, 0, 0, 0}, resource_id, 1,
        framebuffer_physical, static_cast<uint32_t>(bytes), 0};
    if (!command_no_data(&attach, sizeof(attach))) { serial_text("graphics: VirtIO-GPU ATTACH_BACKING failed\r\n"); return false; }
    SetScanout scanout{{0x0103, 0, 0, 0, 0}, {0, 0, display_width, display_height}, 0, resource_id};
    if (!command_no_data(&scanout, sizeof(scanout))) { serial_text("graphics: VirtIO-GPU SET_SCANOUT failed\r\n"); return false; }
    return true;
}

}

bool rawline::kernel::initialize_virtio_gpu(const boot::BootInfo* boot_info)
{
    ready = false;
    framebuffer = nullptr;
    if (boot_info == nullptr || boot_info->version != boot::boot_info_version ||
        boot_info->memory.hhdm_offset == 0 || boot_info->framebuffer.width > UINT32_MAX ||
        boot_info->framebuffer.height > UINT32_MAX) return false;
    hhdm_offset = boot_info->memory.hhdm_offset;
    int found_slot = -1;
    for (uint8_t slot = 0; slot < 32; ++slot) {
        const uint32_t id = pci_read(slot, 0);
        if ((id & 0xffff) == 0x1af4 && (id >> 16) == 0x1050) { found_slot = slot; break; }
    }
    if (found_slot < 0) { serial_text("graphics: VirtIO-GPU not found; using firmware framebuffer\r\n"); return false; }
    const uint8_t slot = static_cast<uint8_t>(found_slot);
    pci_write(slot, 4, pci_read(slot, 4) | 6);
    uint64_t common_address, common_offset, common_length;
    uint64_t notify_address, notify_offset, notify_length;
    uint32_t notify_multiplier32 = 0;
    if (!find_virtio_capability(slot, 1, &common_address, &common_offset, &common_length, nullptr) ||
        common_length < 56 ||
        !find_virtio_capability(slot, 2, &notify_address, &notify_offset, &notify_length, &notify_multiplier32) ||
        notify_multiplier32 == 0) {
        serial_text("graphics: VirtIO-GPU PCI capabilities unavailable; using firmware framebuffer\r\n");
        return false;
    }
    common_cfg = reinterpret_cast<volatile uint8_t*>(common_address);
    queue_notify = reinterpret_cast<volatile uint16_t*>(notify_address);
    notify_multiplier = notify_multiplier32;
    notify_region_length = notify_length;
    (void)common_offset;
    (void)notify_offset;
    if (!initialize_queue()) {
        serial_text("graphics: VirtIO-GPU queue initialization failed; using firmware framebuffer\r\n");
        return false;
    }
    common_cfg[20] = 15;
    if ((common_cfg[20] & 4) == 0) {
        serial_text("graphics: VirtIO-GPU failed DRIVER_OK; using firmware framebuffer\r\n");
        return false;
    }
    ready = true;
    if (!setup_scanout(boot_info)) {
        ready = false;
        serial_text("graphics: VirtIO-GPU scanout setup failed; using firmware framebuffer\r\n");
        return false;
    }
    serial_text("graphics: VirtIO-GPU 2D scanout active ");
    serial_hex(display_width); serial_text("x"); serial_hex(display_height); serial_text("\r\n");
    return true;
}

bool rawline::kernel::virtio_gpu_ready() { return ready; }

bool rawline::kernel::virtio_gpu_get_info(graphics_backend::Info* info)
{
    if (!ready || info == nullptr) return false;
    *info = {graphics_backend::info_version, display_width, display_height, display_width,
        graphics_backend::format_rgbx8888, graphics_backend::backend_virtio_gpu_2d, 1};
    return true;
}

bool rawline::kernel::virtio_gpu_present(const graphics_backend::Region* regions,
    uint64_t region_count, const uint32_t* pixels, uint64_t source_pitch_pixels)
{
    if (!ready || regions == nullptr || pixels == nullptr || region_count == 0 ||
        source_pitch_pixels < display_width) return false;
    for (uint64_t i = 0; i < region_count; ++i) {
        const auto& region = regions[i];
        if (region.x < 0 || region.y < 0 || region.width == 0 || region.height == 0 ||
            static_cast<uint64_t>(region.x) > display_width ||
            static_cast<uint64_t>(region.y) > display_height ||
            region.width > display_width - static_cast<uint64_t>(region.x) ||
            region.height > display_height - static_cast<uint64_t>(region.y)) return false;
        for (uint64_t row = 0; row < region.height; ++row) {
            const uint32_t* source = pixels + (static_cast<uint64_t>(region.y) + row) * source_pitch_pixels +
                static_cast<uint64_t>(region.x);
            uint32_t* target = framebuffer + (static_cast<uint64_t>(region.y) + row) * display_width +
                static_cast<uint64_t>(region.x);
            for (uint64_t column = 0; column < region.width; ++column)
                target[column] = 0xff000000u | (source[column] & 0x00ffffffu);
        }
    }
    asm volatile("mfence" : : : "memory");

    uint64_t region_offset = 0;
    while (region_offset < region_count) {
        const uint64_t batch_limit = queue_size / 4 < command_slot_capacity / 2
            ? queue_size / 4 : command_slot_capacity / 2;
        if (batch_limit == 0) return false;
        const uint64_t batch_regions = region_count - region_offset < batch_limit
            ? region_count - region_offset : batch_limit;
        const uint16_t command_count = static_cast<uint16_t>(batch_regions * 2);
        const uint16_t starting_available = *available_index;
        const uint16_t starting_used = *used_index;

        for (uint64_t i = 0; i < batch_regions; ++i) {
            const auto& region = regions[region_offset + i];
            const Rect rect{static_cast<uint32_t>(region.x), static_cast<uint32_t>(region.y),
                static_cast<uint32_t>(region.width), static_cast<uint32_t>(region.height)};
            const Transfer2d transfer{{0x0105, 0, 0, 0, 0}, rect,
                (static_cast<uint64_t>(region.y) * display_width + static_cast<uint64_t>(region.x)) * 4,
                resource_id, 0};
            const Flush flush{{0x0104, 0, 0, 0, 0}, rect, resource_id, 0};
            const uint64_t transfer_index = i;
            const uint64_t flush_index = batch_regions + i;
            const uint64_t indexes[]{transfer_index, flush_index};
            const void* requests[]{&transfer, &flush};
            const uint32_t lengths[]{sizeof(transfer), sizeof(flush)};
            for (uint32_t kind = 0; kind < 2; ++kind) {
                const uint64_t command_index = indexes[kind];
                const uint64_t slot_offset = command_slot_offset + command_index * command_slot_bytes;
                auto* request = queue_base + slot_offset;
                auto* response = request + 64;
                const auto* request_bytes = static_cast<const uint8_t*>(requests[kind]);
                for (uint32_t byte = 0; byte < lengths[kind]; ++byte) request[byte] = request_bytes[byte];
                for (uint32_t byte = 0; byte < sizeof(Header); ++byte) response[byte] = 0;
                const uint16_t descriptor_index = static_cast<uint16_t>(command_index * 2);
                descriptors[descriptor_index] = {queue_physical + slot_offset, lengths[kind], 1,
                    static_cast<uint16_t>(descriptor_index + 1)};
                descriptors[descriptor_index + 1] = {queue_physical + slot_offset + 64,
                    sizeof(Header), 2, 0};
                available_entries[static_cast<uint16_t>(starting_available + command_index) % queue_size] =
                    descriptor_index;
            }
        }
        asm volatile("mfence" : : : "memory");
        *available_index = static_cast<uint16_t>(starting_available + command_count);
        mmio_write16(reinterpret_cast<volatile uint8_t*>(queue_notify), 0, 0);
        uint64_t spins = 0;
        while (static_cast<uint16_t>(*used_index - starting_used) < command_count &&
            spins++ < 100000000) asm volatile("pause");
        if (static_cast<uint16_t>(*used_index - starting_used) < command_count) return false;
        asm volatile("lfence" : : : "memory");
        for (uint64_t command_index = 0; command_index < command_count; ++command_index) {
            const auto* response = reinterpret_cast<const Header*>(queue_base + command_slot_offset +
                command_index * command_slot_bytes + 64);
            if (response->type != 0x1100) return false;
        }
        region_offset += batch_regions;
    }
    return true;
}

bool rawline::kernel::virtio_gpu_verify_rgb_at(uint64_t x, uint64_t y, uint32_t rgb)
{
    if (!ready || x >= display_width || y >= display_height) return false;
    return (framebuffer[static_cast<uint64_t>(y) * display_width + x] & 0x00ffffffu) == (rgb & 0x00ffffffu);
}
