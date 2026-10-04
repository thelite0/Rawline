#include <rawline/arch/x86_64/interrupts.hpp>
#include <rawline/kernel/scheduler/scheduler.hpp>

namespace rawline::kernel {
Scheduler* active_scheduler;
}

namespace rawline::kernel::arch::x86_64 {

InterruptFrame* schedule_from_timer(InterruptFrame* frame)
{
    if (rawline::kernel::active_scheduler == nullptr) return frame;
    return rawline::kernel::active_scheduler->on_timer(frame);
}

}

namespace rawline::kernel {

void set_active_scheduler(Scheduler* scheduler)
{
    active_scheduler = scheduler;
}

}
