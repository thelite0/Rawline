#include <stdint.h>

#include <rawline/kernel/memory/physical_allocator.hpp>
#include <rawline/kernel/display.hpp>
#include <rawline/storage_module.hpp>

namespace {

constexpr uint16_t pci_address_port = 0xcf8;
constexpr uint16_t pci_data_port = 0xcfc;
constexpr uint64_t page_size = 4096;
constexpr uint16_t maximum_queue_size = 256;
constexpr rawline::resource::OwnerId kernel_owner{1, 1, rawline::resource::OwnerKind::Kernel};

void serial_text(const char* text)
{
    while (*text) {
        uint8_t status;
        asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd}));
        if (status & 0x20) asm volatile("outb %0, %w1" : : "a"(*text++), "d"(uint16_t{0x3f8}));
    }
}

struct Descriptor { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; } __attribute__((packed));
struct UsedElement { uint32_t id; uint32_t length; } __attribute__((packed));
struct Request { uint32_t type; uint32_t reserved; uint64_t sector; } __attribute__((packed));
uint16_t io_base;
uint64_t capacity_sectors;
uint64_t hhdm_offset;
uint64_t queue_physical;
uint8_t* queue_base;
Descriptor* descriptors;
volatile uint16_t* available_index;
uint16_t* available_entries;
volatile uint16_t* used_index;
Request* request;
uint8_t* request_status;
uint16_t last_used_index;
uint16_t queue_entries;
rawline::resource::OwnerId buffer_owner;
bool ready;
bool flush_supported;

uint32_t in32(uint16_t port) { uint32_t value; asm volatile("inl %w1, %0" : "=a"(value) : "d"(port)); return value; }
uint16_t in16(uint16_t port) { uint16_t value; asm volatile("inw %w1, %0" : "=a"(value) : "d"(port)); return value; }
void out32(uint16_t port, uint32_t value) { asm volatile("outl %0, %w1" : : "a"(value), "d"(port)); }
void out16(uint16_t port, uint16_t value) { asm volatile("outw %0, %w1" : : "a"(value), "d"(port)); }
uint8_t in8(uint16_t port) { uint8_t value; asm volatile("inb %w1, %0" : "=a"(value) : "d"(port)); return value; }
void out8(uint16_t port, uint8_t value) { asm volatile("outb %0, %w1" : : "a"(value), "d"(port)); }

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

bool wait_request()
{
    uint64_t spins = 0;
    while (*used_index == last_used_index && spins++ < 50000000) asm volatile("pause");
    if (*used_index == last_used_index) { serial_text("storage kernel: VirtIO request timeout\r\n"); return false; }
    ++last_used_index;
    if (*request_status != 0) serial_text("storage kernel: VirtIO request status error\r\n");
    return *request_status == 0;
}

bool submit(uint32_t type, uint64_t sector, uint64_t data_physical,
    uint32_t data_bytes, bool device_reads_data)
{
    request->type = type;
    request->reserved = 0;
    request->sector = sector;
    *request_status = 0xff;
    auto& d = descriptors[0]; d = {queue_physical + 3 * page_size, sizeof(Request), 1, 1};
    if (data_bytes != 0) {
        descriptors[1] = {data_physical, data_bytes,
            static_cast<uint16_t>(1 | (device_reads_data ? 0 : 2)), 2};
        descriptors[2] = {queue_physical + 3 * page_size + sizeof(Request), 1, 2, 0};
    } else {
        descriptors[1] = {queue_physical + 3 * page_size + sizeof(Request), 1, 2, 0};
    }
    const uint16_t head = 0;
    available_entries[*available_index % queue_entries] = head;
    asm volatile("mfence" : : : "memory");
    *available_index = static_cast<uint16_t>(*available_index + 1);
    out16(io_base + 0x10, 0);
    return wait_request();
}

bool buffer_range(rawline::resource::AllocationHandle handle, uint64_t offset,
    uint64_t bytes, uint64_t* physical)
{
    rawline::memory_module::AllocationInfo info{};
    if (!rawline::kernel::memory::physical_allocator().allocation_info(handle, &info) ||
        info.owner.kind != buffer_owner.kind || info.owner.value != buffer_owner.value ||
        info.owner.generation != buffer_owner.generation ||
        (info.flags & rawline::resource::allocation_flag_shared) == 0 ||
        offset > info.pages * page_size || bytes > info.pages * page_size - offset ||
        (offset & (page_size - 1)) != 0 || offset > UINT64_MAX - info.physical_base) return false;
    *physical = info.physical_base + offset;
    return true;
}

bool get_device(uint64_t device, rawline::block_module::DeviceInfo* info)
{
    if (!ready || device != 0 || info == nullptr) return false;
    *info = {rawline::block_module::api_version, 0, capacity_sectors, 512,
        flush_supported ? rawline::block_module::device_flag_flush : 0};
    return true;
}

bool read_device(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle buffer, uint64_t offset)
{
    uint64_t physical;
    if (!ready || device != 0 || count == 0 || count > 8 || lba >= capacity_sectors ||
        count > capacity_sectors - lba || !buffer_range(buffer, offset, uint64_t{count} * 512, &physical)) return false;
    return submit(0, lba, physical, count * 512, false);
}

bool write_device(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle buffer, uint64_t offset)
{
    uint64_t physical;
    if (!ready || device != 0 || count == 0 || count > 8 || lba >= capacity_sectors ||
        count > capacity_sectors - lba || !buffer_range(buffer, offset, uint64_t{count} * 512, &physical)) return false;
    return submit(1, lba, physical, count * 512, true);
}

bool flush_device(uint64_t device)
{
    return ready && flush_supported && device == 0 && submit(4, 0, 0, 0, false);
}

bool initialize(const rawline::boot::BootInfo* boot, rawline::resource::OwnerId owner)
{
    ready = false;
    if (boot == nullptr || boot->version != rawline::boot::boot_info_version ||
        boot->memory.hhdm_offset == 0 || owner.kind != rawline::resource::OwnerKind::Shared ||
        owner.value == 0 || owner.generation == 0) return false;
    buffer_owner = owner;
    hhdm_offset = boot->memory.hhdm_offset;
    int found_slot = -1;
    for (uint8_t slot = 0; slot < 32; ++slot) {
        const uint32_t id = pci_read(slot, 0);
        if ((id & 0xffff) == 0x1af4 && (id >> 16) == 0x1001) { found_slot = slot; break; }
    }
    if (found_slot < 0) { serial_text("storage kernel: transitional VirtIO PCI device not found\r\n"); return false; }
    serial_text("storage kernel: VirtIO PCI device found\r\n");
    const uint8_t slot = static_cast<uint8_t>(found_slot);
    const uint32_t bar = pci_read(slot, 0x10);
    if ((bar & 1) == 0) { serial_text("storage kernel: VirtIO BAR is not I/O\r\n"); return false; }
    io_base = static_cast<uint16_t>(bar & 0xfffcu);
    pci_write(slot, 4, pci_read(slot, 4) | 5);
    out8(io_base + 0x12, 0);
    out8(io_base + 0x12, 3);
    const uint32_t host_features = in32(io_base);
    flush_supported = (host_features & (1u << 9)) != 0;
    out32(io_base + 4, host_features & (1u << 9));
    out8(io_base + 0x12, 11);
    if ((in8(io_base + 0x12) & 8) == 0) { serial_text("storage kernel: feature negotiation failed\r\n"); return false; }
    out16(io_base + 0x0e, 0);
    queue_entries = in16(io_base + 0x0c);
    if (queue_entries == 0 || queue_entries > maximum_queue_size) {
        serial_text("storage kernel: unsupported virtqueue size\r\n"); return false;
    }

    auto& allocator = rawline::kernel::memory::physical_allocator();
    const auto allocation = allocator.allocate_pages(kernel_owner, 4,
        rawline::resource::allocation_flag_kernel_lifetime);
    if (allocation.generation == 0 || !allocator.physical_address(allocation, &queue_physical)) { serial_text("storage kernel: queue allocation failed\r\n"); return false; }
    if (queue_physical >= 0x100000000ull || hhdm_offset > UINT64_MAX - queue_physical) { serial_text("storage kernel: queue address is outside legacy DMA range\r\n"); return false; }
    queue_base = reinterpret_cast<uint8_t*>(hhdm_offset + queue_physical);
    request = reinterpret_cast<Request*>(hhdm_offset + queue_physical + 0x3000);
    request_status = reinterpret_cast<uint8_t*>(request + 1);
    for (uint64_t i = 0; i < 4 * page_size; ++i)
        reinterpret_cast<volatile uint8_t*>(queue_base)[i] = 0;
    descriptors = reinterpret_cast<Descriptor*>(queue_base);
    const uint64_t available_offset = uint64_t{queue_entries} * sizeof(Descriptor);
    auto* available = queue_base + available_offset;
    available_index = reinterpret_cast<volatile uint16_t*>(available + 2);
    available_entries = reinterpret_cast<uint16_t*>(available + 4);
    const uint64_t used_offset = (available_offset + 4 + uint64_t{queue_entries} * 2 + page_size - 1) &
        ~(page_size - 1);
    auto* used = queue_base + used_offset;
    used_index = reinterpret_cast<volatile uint16_t*>(used + 2);
    out32(io_base + 8, static_cast<uint32_t>(queue_physical / page_size));
    out8(io_base + 0x12, 15);
    if ((in8(io_base + 0x12) & 4) == 0) { serial_text("storage kernel: device did not enter DRIVER_OK\r\n"); return false; }
    capacity_sectors = in32(io_base + 0x14) | (uint64_t{in32(io_base + 0x18)} << 32);
    ready = capacity_sectors > 0;
    if (!ready) serial_text("storage kernel: zero capacity\r\n");
    else serial_text("storage kernel: capacity ready\r\n");
    return ready;
}

bool allocate_buffer(uint64_t pages, rawline::resource::AllocationHandle* handle, void** address)
{
    if (!ready || pages == 0 || handle == nullptr || address == nullptr || buffer_owner.value == 0)
        return false;
    auto& allocator = rawline::kernel::memory::physical_allocator();
    *handle = allocator.allocate_pages(buffer_owner, pages,
        rawline::resource::allocation_flag_shared);
    uint64_t physical;
    if (handle->generation == 0 || !allocator.physical_address(*handle, &physical) ||
        hhdm_offset > UINT64_MAX - physical) return false;
    *address = reinterpret_cast<void*>(hhdm_offset + physical);
    return true;
}

bool release_buffer(rawline::resource::AllocationHandle handle)
{
    rawline::memory_module::AllocationInfo info{};
    auto& allocator = rawline::kernel::memory::physical_allocator();
    return allocator.allocation_info(handle, &info) && info.owner.value == buffer_owner.value &&
        info.owner.generation == buffer_owner.generation && info.owner.kind == buffer_owner.kind &&
        (info.flags & rawline::resource::allocation_flag_shared) != 0 &&
        allocator.release_shared(handle);
}

}

bool rawline::kernel::initialize_block_device(const rawline::boot::BootInfo* boot_info,
    rawline::resource::OwnerId owner)
{
    return initialize(boot_info, owner);
}

const rawline::block_module::Api* rawline::kernel::block_device_api()
{
    static const block_module::Api api{
        block_module::api_version, sizeof(block_module::Api),
        []() -> uint64_t { return ready ? 1 : 0; }, get_device,
        read_device, write_device, flush_device, allocate_buffer, release_buffer, nullptr
    };
    return &api;
}
