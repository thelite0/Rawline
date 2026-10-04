#pragma once

#include <stddef.h>

#include <rawline/kernel/thread/thread.hpp>

namespace rawline::kernel {

class Scheduler {
public:
    static constexpr size_t maximum_threads = 5;
    static constexpr size_t stack_size = 16 * 1024;

    Scheduler();
    Thread* create_thread(process::ProcessId process_id, void (*entry)(void*),
        void* argument = nullptr, bool high_priority = false);
    arch::x86_64::InterruptFrame* on_timer(arch::x86_64::InterruptFrame* frame);

private:
    Thread threads_[maximum_threads];
    alignas(16) uint8_t stacks_[maximum_threads][stack_size];
    size_t count_;
    size_t next_normal_index_;
    size_t next_high_index_;
    size_t current_index_;
    ThreadId next_id_;
    uint8_t high_priority_budget_;
};

void set_active_scheduler(Scheduler* scheduler);

}
