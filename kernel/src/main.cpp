#include <stddef.h>
#include <stdint.h>

#include <new>

#include <rawline/arch/x86_64/interrupts.hpp>
#include <rawline/arch/x86_64/mouse.hpp>
#include <rawline/boot_info.hpp>
#include <rawline/kernel/process/process_manager.hpp>
#include <rawline/kernel/display.hpp>
#include <rawline/kernel/graphics.hpp>
#include <rawline/kernel/memory/physical_allocator.hpp>
#include <rawline/kernel/rwl_loader.hpp>
#include <rawline/kernel/scheduler/scheduler.hpp>
#include <rawline/kernel_module.hpp>

namespace {

void serial_text(const char* text)
{
    while (*text) {
        uint8_t status;
        asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd}));
        if ((status & 0x20) == 0) continue;
        asm volatile("outb %0, %w1" : : "a"(*text++), "d"(uint16_t{0x3f8}));
    }
}

void serial_hex(uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    char buffer[17];
    buffer[16] = '\0';
    for (int i = 15; i >= 0; --i) {
        buffer[i] = digits[value & 0xf];
        value >>= 4;
    }
    serial_text(buffer);
}

void module_write_serial(const char* text)
{
    if (text != nullptr) serial_text(text);
}

void thread_a(void*)
{
    uint64_t count = 0;
    uint64_t last_report_tick = rawline::kernel::arch::x86_64::timer_tick_count();
    for (;;) {
        if ((count++ & 0xfffff) == 0) {
            const uint64_t tick = rawline::kernel::arch::x86_64::timer_tick_count();
            if (tick - last_report_tick >= 4000) {
                last_report_tick = tick;
                serial_text("A:"); serial_hex(count); serial_text("\r\n");
            }
        }
    }
}

void thread_b(void*)
{
    uint64_t count = 0;
    uint64_t last_report_tick = rawline::kernel::arch::x86_64::timer_tick_count();
    for (;;) {
        if ((count++ & 0xfffff) == 0) {
            const uint64_t tick = rawline::kernel::arch::x86_64::timer_tick_count();
            if (tick - last_report_tick >= 4000) {
                last_report_tick = tick;
                serial_text("B:"); serial_hex(count); serial_text("\r\n");
            }
        }
    }
}

rawline::kernel::process::ProcessManager process_manager;
rawline::resource::OwnerId block_buffer_owner;
alignas(rawline::kernel::Scheduler) uint8_t scheduler_storage[sizeof(rawline::kernel::Scheduler)];
rawline::kernel::Scheduler* scheduler;
rawline::kernel::process::ProcessId kernel_process_id;

struct ModuleThreadLaunch {
    void (*entry)(void*);
    void* context;
};

ModuleThreadLaunch module_thread_launches[rawline::kernel::Scheduler::maximum_threads - 2];
size_t module_thread_count;

extern "C" int64_t rawline_invoke_rwl(int64_t (*entry)(const void*), const void* context);

int64_t invoke_module_thread(const void* context)
{
    auto* launch = static_cast<ModuleThreadLaunch*>(const_cast<void*>(context));
    launch->entry(launch->context);
    return 0;
}

void module_thread_trampoline(void* context)
{
    const int64_t result = rawline_invoke_rwl(invoke_module_thread, context);
    if (result != 0) serial_text("kernel: privileged module thread fault contained\r\n");
    for (;;) asm volatile("sti; hlt");
}

bool create_module_thread(void (*entry)(void*), void* context)
{
    if (entry == nullptr || scheduler == nullptr || kernel_process_id == 0 ||
        module_thread_count >= rawline::kernel::Scheduler::maximum_threads - 2) return false;
    auto* launch = &module_thread_launches[module_thread_count];
    launch->entry = entry;
    launch->context = context;
    if (scheduler->create_thread(kernel_process_id, module_thread_trampoline, launch, true) == nullptr)
        return false;
    ++module_thread_count;
    return true;
}

bool poll_pointer_event(rawline::pointer::Event* event)
{
    return rawline::kernel::arch::x86_64::poll_mouse_event(event);
}

bool dispatch_pointer_event(const rawline::pointer::Event* event)
{
    return rawline::kernel::dispatch_pointer_event(event);
}

uint64_t monotonic_ns()
{
    return rawline::kernel::arch::x86_64::monotonic_nanoseconds();
}

void wait_for_interrupt()
{
    asm volatile("sti; hlt" : : : "memory");
}

uint8_t read_cmos(uint8_t index)
{
    asm volatile("outb %0, %w1" : : "a"(static_cast<uint8_t>(index | 0x80)), "d"(uint16_t{0x70}));
    uint8_t value;
    asm volatile("inb %w1, %0" : "=a"(value) : "d"(uint16_t{0x71}));
    return value;
}

uint8_t bcd_to_binary(uint8_t value)
{
    return static_cast<uint8_t>((value & 0x0f) + ((value >> 4) * 10));
}

bool read_wall_clock(rawline::wall_clock::Time* time)
{
    if (time == nullptr) return false;
    uint8_t second = 0, minute = 0, hour = 0, status_b = 0;
    bool stable = false;
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if ((read_cmos(0x0a) & 0x80) != 0) continue;
        const uint8_t first_second = read_cmos(0x00);
        const uint8_t first_minute = read_cmos(0x02);
        const uint8_t first_hour = read_cmos(0x04);
        const uint8_t first_status = read_cmos(0x0b);
        if ((read_cmos(0x0a) & 0x80) != 0) continue;
        if (first_second != read_cmos(0x00) || first_minute != read_cmos(0x02) ||
            first_hour != read_cmos(0x04)) continue;
        second = first_second;
        minute = first_minute;
        hour = first_hour;
        status_b = first_status;
        stable = true;
        break;
    }
    if (!stable) return false;
    const bool binary = (status_b & 0x04) != 0;
    const bool mode_24_hour = (status_b & 0x02) != 0;
    const bool pm = (hour & 0x80) != 0;
    hour &= 0x7f;
    if (!binary) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
    }
    if (!mode_24_hour) {
        hour = static_cast<uint8_t>(hour % 12);
        if (pm) hour = static_cast<uint8_t>(hour + 12);
    }
    if (hour > 23 || minute > 59 || second > 59) return false;
    *time = {hour, minute, second, 0};
    return true;
}

bool initialize_block(const rawline::boot::BootInfo* boot_info, rawline::resource::OwnerId owner)
{
    if (!rawline::kernel::initialize_block_device(boot_info, owner)) return false;
    block_buffer_owner = owner;
    return true;
}

bool block_info(uint64_t device, rawline::block_module::DeviceInfo* info)
{
    return rawline::kernel::block_device_api()->get_device_info(device, info);
}

bool block_read(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle buffer, uint64_t offset)
{
    return rawline::kernel::block_device_api()->read_blocks(device, lba, count, buffer, offset);
}

bool block_write(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle buffer, uint64_t offset)
{
    return rawline::kernel::block_device_api()->write_blocks(device, lba, count, buffer, offset);
}

bool block_flush(uint64_t device)
{
    return rawline::kernel::block_device_api()->flush(device);
}

bool allocate_dma_buffer(rawline::resource::OwnerId owner, uint64_t pages,
    rawline::resource::AllocationHandle* handle, void** address)
{
    if (handle == nullptr || address == nullptr || pages == 0 ||
        owner.kind != rawline::resource::OwnerKind::Shared || owner.value != block_buffer_owner.value ||
        owner.generation != block_buffer_owner.generation || block_buffer_owner.value == 0) return false;
    return rawline::kernel::block_device_api()->allocate_buffer(pages, handle, address);
}

bool release_dma_buffer(rawline::resource::AllocationHandle handle)
{
    rawline::memory_module::AllocationInfo info{};
    auto& allocator = rawline::kernel::memory::physical_allocator();
    return allocator.allocation_info(handle, &info) && info.owner.value == block_buffer_owner.value &&
        info.owner.generation == block_buffer_owner.generation && info.owner.kind == block_buffer_owner.kind &&
        (info.flags & rawline::resource::allocation_flag_shared) != 0 && allocator.release_shared(handle);
}

extern "C" [[noreturn]] void rawline_main(const rawline::boot::BootInfo* boot);

}

extern "C" [[noreturn]] void rawline_kernel_entry(const rawline::boot::BootInfo* boot)
{
    new (&process_manager) rawline::kernel::process::ProcessManager();
    rawline_main(boot);
}

extern "C" [[noreturn]]
void rawline_main(const rawline::boot::BootInfo* boot)
{
    serial_text("RAWLINE RWL kernel entered\r\n");
    if (boot == nullptr) {
        rawline::kernel::arch::x86_64::fatal_halt();
    }

    if (boot->version != rawline::boot::boot_info_version) {
        rawline::kernel::arch::x86_64::fatal_halt();
    }

    auto& physical_allocator = rawline::kernel::memory::physical_allocator();
    serial_text("kernel: initializing physical ownership map\r\n");
    if (!physical_allocator.initialize(boot)) {
        serial_text("kernel: physical ownership initialization failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    if (!rawline::kernel::initialize_display(boot)) {
        serial_text("kernel: framebuffer initialization failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    if (!rawline::kernel::initialize_graphics(boot)) {
        serial_text("kernel: no usable graphics backend\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    serial_text("kernel: physical ownership map ready\r\n");
    const auto domain = physical_allocator.create_domain(rawline::resource::OwnerKind::Domain);
    const rawline::resource::OwnerId test_owner = rawline::resource::owner_id(domain);
    const auto recipient_domain = physical_allocator.create_domain(rawline::resource::OwnerKind::Process);
    const rawline::resource::OwnerId recipient_owner = rawline::resource::owner_id(recipient_domain);
    rawline::memory_module::Stats before{};
    rawline::memory_module::Stats during{};
    rawline::memory_module::Stats after{};
    rawline::memory_module::OwnerStats test_stats{};
    if (!physical_allocator.statistics(&before))
        rawline::kernel::arch::x86_64::fatal_halt();
    const auto first_test_allocation = physical_allocator.allocate_pages(test_owner, 3);
    const auto second_test_allocation = physical_allocator.allocate_pages(test_owner, 5);
    if (domain.value == 0 || first_test_allocation.generation == 0 ||
        second_test_allocation.generation == 0 || recipient_domain.value == 0 ||
        !physical_allocator.query_owner(test_owner, &test_stats) || test_stats.used_pages != 8 ||
        test_stats.allocation_count != 2 || !physical_allocator.statistics(&during) ||
        during.free_pages + 8 != before.free_pages ||
        !physical_allocator.transfer_owner(test_owner, recipient_owner) ||
        !physical_allocator.query_owner(test_owner, &test_stats) || test_stats.used_pages != 0 ||
        !physical_allocator.query_owner(recipient_owner, &test_stats) || test_stats.used_pages != 8 ||
        !physical_allocator.destroy_owner_domain(test_owner) ||
        !physical_allocator.destroy_owner_domain(recipient_owner) ||
        physical_allocator.release(first_test_allocation) ||
        !physical_allocator.statistics(&after) || after.free_pages != before.free_pages ||
        after.used_pages != before.used_pages) {
        serial_text("kernel: temporary domain reclaim verification failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    const auto shared_domain = physical_allocator.create_domain(rawline::resource::OwnerKind::Shared);
    const rawline::resource::OwnerId shared_owner = rawline::resource::owner_id(shared_domain);
    const auto shared_allocation = physical_allocator.allocate_pages(shared_owner, 2,
        rawline::resource::allocation_flag_shared);
    if (shared_domain.value == 0 || shared_allocation.generation == 0 ||
        !physical_allocator.retain_shared(shared_allocation) ||
        physical_allocator.destroy_owner_domain(shared_owner) ||
        !physical_allocator.release_shared(shared_allocation) ||
        !physical_allocator.destroy_owner_domain(shared_owner) ||
        !physical_allocator.statistics(&after) || after.free_pages != before.free_pages ||
        after.used_pages != before.used_pages) {
        serial_text("kernel: shared owner verification failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    serial_text("kernel: owner transfer, shared references and 8-page reclaim verified\r\n");

    (void)process_manager;

    scheduler = new (scheduler_storage) rawline::kernel::Scheduler();
    rawline::kernel::set_active_scheduler(scheduler);
    auto* kernel_process = process_manager.create("kernel");
    if (kernel_process == nullptr) {
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    kernel_process_id = kernel_process->pid;
    if (scheduler->create_thread(kernel_process->pid, thread_a) == nullptr ||
        scheduler->create_thread(kernel_process->pid, thread_b) == nullptr) {
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    rawline::kernel::arch::x86_64::initialize_interrupts();
    rawline::kernel::arch::x86_64::initialize_timer();
    if (rawline::kernel::arch::x86_64::initialize_mouse())
        serial_text("x86_64: PS/2 mouse initialized\r\n");
    else
        serial_text("x86_64: PS/2 mouse unavailable\r\n");
    const rawline::kernel_module::Api module_api{
        rawline::kernel_module::api_version,
        sizeof(rawline::kernel_module::Api),
        module_write_serial,
        rawline::kernel::find_boot_module,
        rawline::kernel::start_rwl_module,
        rawline::kernel::get_memory_stats,
        rawline::kernel::get_memory_owner_stats,
        rawline::kernel::get_memory_owner_allocations,
        rawline::kernel::create_memory_domain,
        rawline::kernel::allocate_memory_pages,
        rawline::kernel::release_memory_allocation,
        rawline::kernel::destroy_memory_domain,
        rawline::kernel::transfer_memory_owner,
        rawline::kernel::retain_shared_memory,
        rawline::kernel::release_shared_memory,
        initialize_block,
        block_info,
        block_read,
        block_write,
        block_flush,
        allocate_dma_buffer,
        release_dma_buffer,
        read_wall_clock
    };
    const rawline::display_module::Api display_api{
        rawline::display_module::api_version,
        sizeof(rawline::display_module::Api),
        module_write_serial,
        rawline::kernel::register_pointer_handler,
        create_module_thread,
        monotonic_ns,
        wait_for_interrupt,
        rawline::kernel::arch::x86_64::timer_tick_count
    };
    const rawline::input_module::Api input_api{
        rawline::input_module::api_version,
        sizeof(rawline::input_module::Api),
        poll_pointer_event,
        dispatch_pointer_event,
        create_module_thread,
        module_write_serial,
        wait_for_interrupt
    };
    const rawline::kernel_module::Context module_context{
        rawline::kernel_module::context_version,
        sizeof(rawline::kernel_module::Context),
        &module_api,
        boot,
        &display_api,
        &input_api,
        rawline::kernel::graphics_backend_api()
    };
    const auto* miraboot = rawline::kernel::find_boot_module(boot, "miraboot.rwl");
    if (miraboot == nullptr ||
        rawline::kernel::start_rwl_module(boot, miraboot, &module_context) != 0) {
        serial_text("kernel: miraboot load or execution failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    rawline::graphics_backend::Info display_info{};
    if (!rawline::kernel::graphics_backend_api()->get_info(&display_info) || display_info.width < 8 ||
        display_info.height < 16) {
        serial_text("kernel: compositor framebuffer proof unavailable\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    const uint64_t window_width = display_info.width < 320 ? display_info.width / 2 : 320;
    const uint64_t window_height = display_info.height < 220 ? display_info.height / 2 : 220;
    const uint64_t window_x = (display_info.width - window_width) / 2;
    const uint64_t window_y = (display_info.height - window_height) / 2;
    if (window_width == 0 || window_height == 0 ||
        !rawline::kernel::verify_graphics_rgb_at(0, 10, 0x20354b) ||
        !rawline::kernel::verify_graphics_rgb_at(window_x, window_y, 0x354b63) ||
        !rawline::kernel::verify_graphics_rgb_at(window_x + 14, window_y + 13, 0xf5f8fc) ||
        !rawline::kernel::verify_graphics_rgb_at(24, 38, 0xd4e0eb)) {
        serial_text("kernel: compositor framebuffer pixel verification failed\r\n");
        rawline::kernel::arch::x86_64::fatal_halt();
    }
    serial_text("kernel: compositor background, window and text pixels verified\r\n");
    serial_text("RAWLINE Miraboot returned to kernel\r\n");
    serial_text("scheduler: enabling timer preemption\r\n");
    for (;;) asm volatile("sti; hlt");
}
