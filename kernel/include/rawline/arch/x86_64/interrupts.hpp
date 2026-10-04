#pragma once

#include <rawline/arch/x86_64/interrupt_frame.hpp>

namespace rawline::kernel::arch::x86_64 {

void initialize_interrupts();
void initialize_timer();
uint64_t timer_tick_count();
uint64_t monotonic_nanoseconds();
InterruptFrame* dispatch_interrupt(InterruptFrame* frame);
InterruptFrame* schedule_from_timer(InterruptFrame* frame);
[[noreturn]] void fatal_halt();

}
