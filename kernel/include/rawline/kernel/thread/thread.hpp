#pragma once

#include <stddef.h>
#include <stdint.h>

#include <rawline/kernel/process/process.hpp>
#include <rawline/arch/x86_64/interrupt_frame.hpp>

namespace rawline::kernel {

using ThreadId = uint64_t;

enum class ThreadState : uint8_t {
    Created,
    Ready,
    Running,
    Blocked,
    Exited
};

struct Thread {
    ThreadId id;
    process::ProcessId process_id;
    ThreadState state;
    arch::x86_64::InterruptFrame* context;
    uint8_t* stack;
    size_t stack_size;
    bool high_priority;
    uint64_t rwl_recovery_active;
    uint64_t rwl_recovery_rip;
    uint64_t rwl_recovery_rsp;
};

}
