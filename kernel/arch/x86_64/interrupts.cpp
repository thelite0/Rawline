#include <stdint.h>

#include <rawline/arch/x86_64/interrupt_frame.hpp>
#include <rawline/arch/x86_64/interrupts.hpp>

namespace rawline::kernel::arch::x86_64 {
namespace {

struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

alignas(16) IdtEntry idt[256]{};
volatile uint64_t timer_interrupts;
volatile bool calibrating_scheduler_timer;
bool logged_first_scheduler_irq;
uint64_t tsc_frequency_hz;
extern "C" const uint64_t rawline_isr_table[33];
extern "C" void rawline_isr_255();
extern "C" volatile uint64_t rawline_rwl_recovery_active;
extern "C" volatile uint64_t rawline_rwl_recovery_rip;

void set_gate(uint8_t vector, uint64_t address)
{
    uint16_t selector;
    asm volatile("mov %%cs, %0" : "=r"(selector));
    idt[vector] = {
        .offset_low = static_cast<uint16_t>(address),
        .selector = selector,
        .ist = 0,
        .attributes = 0x8e,
        .offset_middle = static_cast<uint16_t>(address >> 16),
        .offset_high = static_cast<uint32_t>(address >> 32),
        .reserved = 0
    };
}

void serial_char(char value)
{
    uint8_t status;
    do { asm volatile("inb %w1, %0" : "=a"(status) : "d"(uint16_t{0x3fd})); }
    while ((status & 0x20) == 0);
    asm volatile("outb %0, %w1" : : "a"(value), "d"(uint16_t{0x3f8}));
}

void serial_text(const char* text)
{
    while (*text) serial_char(*text++);
}

void serial_hex(uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) serial_char(digits[(value >> shift) & 0xf]);
}

void out8(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %w1" : : "a"(value), "d"(port));
}

volatile uint32_t* local_apic()
{
    return reinterpret_cast<volatile uint32_t*>(0xffffffffd0000000ull);
}

void apic_write(uint32_t offset, uint32_t value)
{
    local_apic()[offset / sizeof(uint32_t)] = value;
}

uint32_t apic_read(uint32_t offset)
{
    return local_apic()[offset / sizeof(uint32_t)];
}

uint64_t read_tsc_cycles()
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32) | low;
}

}

void initialize_interrupts()
{
    asm volatile("cli");
    for (uint8_t vector = 0; vector < 33; ++vector)
        set_gate(vector, rawline_isr_table[vector]);
    set_gate(255, reinterpret_cast<uint64_t>(&rawline_isr_255));
    const IdtPointer pointer{sizeof(idt) - 1, reinterpret_cast<uint64_t>(idt)};
    asm volatile("lidt %0" : : "m"(pointer));
    serial_text("x86_64: IDT installed\r\n");
}

void initialize_timer()
{
    asm volatile("cli");
    uint32_t apic_base_low, apic_base_high;
    asm volatile("rdmsr" : "=a"(apic_base_low), "=d"(apic_base_high) : "c"(uint32_t{0x1b}));
    apic_base_low = (apic_base_low | (1u << 11)) & ~(1u << 10);
    asm volatile("wrmsr" : : "a"(apic_base_low), "d"(apic_base_high), "c"(uint32_t{0x1b}));

    out8(0x21, 0xff);
    out8(0xa1, 0xff);
    apic_write(0xf0, 0x100u | 0xffu);
    apic_write(0x3e0, 0x3);
    // Calibrate TSC against PIT channel 2 before enabling scheduling.
    apic_write(0x320, 0x10000u | 32u); // masked one-shot calibration
    apic_write(0x380, 0xffffffffu);
    uint8_t speaker;
    asm volatile("inb %w1, %0" : "=a"(speaker) : "d"(uint16_t{0x61}));
    out8(0x61, static_cast<uint8_t>((speaker & ~0x02u) | 0x01u));
    out8(0x43, 0xb0);
    out8(0x42, 0x00); out8(0x42, 0x80);
    uint32_t tsc_start_low, tsc_start_high;
    asm volatile("rdtsc" : "=a"(tsc_start_low), "=d"(tsc_start_high));
    const uint64_t tsc_start = (static_cast<uint64_t>(tsc_start_high) << 32) | tsc_start_low;
    uint16_t count = 0x8000;
    uint64_t guard = 0;
    while (count >= 0x1000 && guard++ < 100000000) {
        out8(0x43, 0x80); // latch channel 2 count
        uint8_t lo, hi;
        asm volatile("inb %w1, %0" : "=a"(lo) : "d"(uint16_t{0x42}));
        asm volatile("inb %w1, %0" : "=a"(hi) : "d"(uint16_t{0x42}));
        count = static_cast<uint16_t>((hi << 8) | lo);
    }
    uint32_t tsc_end_low, tsc_end_high;
    asm volatile("rdtsc" : "=a"(tsc_end_low), "=d"(tsc_end_high));
    const uint64_t tsc_end = (static_cast<uint64_t>(tsc_end_high) << 32) | tsc_end_low;
    const uint64_t measured_tsc_hz = (tsc_end - tsc_start) * 1193182 / 28672;

    // Read the architectural frequency as a fallback, then prefer the PIT
    // measurement so compositor deadlines use the measured hardware clock.
    uint32_t max_leaf, a, b, c, d;
    asm volatile("cpuid" : "=a"(max_leaf), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    if (max_leaf >= 0x15) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x15));
        if (a != 0 && b != 0 && c != 0) tsc_frequency_hz = static_cast<uint64_t>(c) * b / a;
    }
    if (measured_tsc_hz >= 1000000) tsc_frequency_hz = measured_tsc_hz;
    if (tsc_frequency_hz < 1000000) tsc_frequency_hz = 1000000000;

    // Measure the actual periodic APIC interrupt rate against the calibrated
    // TSC. The one-shot current-count register is not a reliable rate source
    // under QEMU, and scheduler frequency is independent from GUI pacing.
    constexpr uint64_t calibration_initial_count = 1000;
    constexpr uint64_t target_scheduler_hz = 4000;
    const uint64_t ticks_before_calibration = timer_interrupts;
    calibrating_scheduler_timer = true;
    apic_write(0x320, 0x20000u | 32u);
    apic_write(0x380, static_cast<uint32_t>(calibration_initial_count));
    asm volatile("sti" : : : "memory");
    const uint64_t calibration_tsc_start = read_tsc_cycles();
    const uint64_t calibration_window = tsc_frequency_hz / 5;
    uint64_t calibration_tsc_end;
    do { calibration_tsc_end = read_tsc_cycles(); }
    while (calibration_tsc_end - calibration_tsc_start < calibration_window);
    asm volatile("cli" : : : "memory");
    calibrating_scheduler_timer = false;
    const uint64_t calibration_ticks = timer_interrupts - ticks_before_calibration;
    const uint64_t calibration_cycles = calibration_tsc_end - calibration_tsc_start;
    const uint64_t calibrated_irq_hz = calibration_ticks * tsc_frequency_hz / calibration_cycles;
    // The measured rate corresponds to calibration_initial_count, so scale
    // the APIC count inversely with the desired interrupt frequency.
    uint64_t initial_count = calibration_initial_count * calibrated_irq_hz / target_scheduler_hz;
    if (initial_count == 0 || initial_count > UINT32_MAX) initial_count = 1000;
    serial_text("x86_64: calibrated scheduler Hz="); serial_hex(calibrated_irq_hz);
    serial_text(" scheduler initial count="); serial_hex(initial_count); serial_text("\r\n");
    serial_text("x86_64: monotonic TSC Hz="); serial_hex(tsc_frequency_hz); serial_text("\r\n");
    serial_text("x86_64: PIT measured TSC Hz="); serial_hex(measured_tsc_hz); serial_text("\r\n");
    apic_write(0x320, 0x20000u | 32u);
    apic_write(0x380, static_cast<uint32_t>(initial_count));
    serial_text("x86_64: local APIC periodic timer armed\r\n");
}

uint64_t timer_tick_count()
{
    return timer_interrupts;
}

uint64_t monotonic_nanoseconds()
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    const uint64_t cycles = (static_cast<uint64_t>(high) << 32) | low;
    static uint64_t epoch_tsc;
    static uint64_t epoch_ns;
    if (epoch_tsc == 0) epoch_tsc = cycles;
    const uint64_t delta = cycles - epoch_tsc;
    const uint64_t whole = delta / tsc_frequency_hz;
    const uint64_t remainder = delta % tsc_frequency_hz;
    const uint64_t result = epoch_ns + whole * 1000000000 + remainder * 1000000000 / tsc_frequency_hz;
    if (result < epoch_ns) return epoch_ns;
    return result;
}

[[noreturn]] void fatal_halt()
{
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

InterruptFrame* dispatch_interrupt(InterruptFrame* frame)
{
    if (frame->vector == 255) return frame;
    if (frame->vector == 32) {
        apic_write(0xb0, 0);
        const uint64_t tick = timer_interrupts;
        timer_interrupts = tick + 1;
        if (!calibrating_scheduler_timer && !logged_first_scheduler_irq) {
            logged_first_scheduler_irq = true;
            serial_text("x86_64: first scheduler timer IRQ\r\n");
        }
        if (calibrating_scheduler_timer) return frame;
        return schedule_from_timer(frame);
    }

    if (rawline_rwl_recovery_active != 0 &&
        (frame->vector == 0 || frame->vector == 6 || frame->vector == 13 || frame->vector == 14)) {
        serial_text("kernel: privileged RWL fault contained rip=");
        serial_hex(frame->rip);
        serial_text(" vector=");
        serial_hex(frame->vector);
        serial_text("\r\n");
        rawline_rwl_recovery_active = 0;
        frame->rip = rawline_rwl_recovery_rip;
        return frame;
    }

    serial_text("FATAL x86 exception vector="); serial_hex(frame->vector);
    serial_text(" error="); serial_hex(frame->error_code);
    serial_text(" rip="); serial_hex(frame->rip);
    if (frame->vector == 14) {
        uint64_t fault_address;
        asm volatile("mov %%cr2, %0" : "=r"(fault_address));
        serial_text(" cr2="); serial_hex(fault_address);
    }
    serial_text("\r\n");
    fatal_halt();
}

}

extern "C" rawline::kernel::arch::x86_64::InterruptFrame*
rawline_interrupt_dispatch(rawline::kernel::arch::x86_64::InterruptFrame* frame)
{
    return rawline::kernel::arch::x86_64::dispatch_interrupt(frame);
}
