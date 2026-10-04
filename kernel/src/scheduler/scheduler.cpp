#include <rawline/kernel/scheduler/scheduler.hpp>

namespace rawline::kernel {
namespace {

extern "C" volatile uint64_t rawline_rwl_recovery_active;
extern "C" volatile uint64_t rawline_rwl_recovery_rip;
extern "C" volatile uint64_t rawline_rwl_recovery_rsp;

[[noreturn]] void thread_returned()
{
    for (;;) asm volatile("cli; hlt");
}

uint16_t current_code_selector()
{
    uint16_t selector;
    asm volatile("mov %%cs, %0" : "=r"(selector));
    return selector;
}

uint16_t current_stack_selector()
{
    uint16_t selector;
    asm volatile("mov %%ss, %0" : "=r"(selector));
    return selector;
}

}

Scheduler::Scheduler()
    : count_(0), next_normal_index_(0), next_high_index_(0),
      current_index_(maximum_threads), next_id_(1),
      high_priority_budget_(4)
{
    for (size_t i = 0; i < maximum_threads; ++i) {
        threads_[i].id = 0;
        threads_[i].process_id = 0;
        threads_[i].state = ThreadState::Created;
        threads_[i].context = nullptr;
        threads_[i].stack = nullptr;
        threads_[i].stack_size = 0;
        threads_[i].high_priority = false;
        threads_[i].rwl_recovery_active = 0;
        threads_[i].rwl_recovery_rip = 0;
        threads_[i].rwl_recovery_rsp = 0;
    }
}

Thread* Scheduler::create_thread(process::ProcessId process_id,
    void (*entry)(void*), void* argument, bool high_priority)
{
    if (process_id == 0 || entry == nullptr || count_ == maximum_threads) return nullptr;

    const size_t index = count_++;
    Thread& thread = threads_[index];
    thread.id = next_id_++;
    thread.process_id = process_id;
    thread.state = ThreadState::Ready;
    thread.stack = stacks_[index];
    thread.stack_size = stack_size;
    thread.high_priority = high_priority;

    const uintptr_t stack_top = reinterpret_cast<uintptr_t>(thread.stack + stack_size);
    auto* return_slot = reinterpret_cast<uint64_t*>(stack_top - sizeof(uint64_t));
    *return_slot = reinterpret_cast<uint64_t>(&thread_returned);

    thread.context = reinterpret_cast<arch::x86_64::InterruptFrame*>(
        stack_top - sizeof(uint64_t) - sizeof(arch::x86_64::InterruptFrame));
    thread.context->r15 = 0; thread.context->r14 = 0;
    thread.context->r13 = 0; thread.context->r12 = 0;
    thread.context->r11 = 0; thread.context->r10 = 0;
    thread.context->r9 = 0; thread.context->r8 = 0;
    thread.context->rdi = reinterpret_cast<uint64_t>(argument); thread.context->rsi = 0;
    thread.context->rbp = 0; thread.context->rdx = 0;
    thread.context->rcx = 0; thread.context->rbx = 0;
    thread.context->rax = 0;
    thread.context->vector = 32;
    thread.context->rip = reinterpret_cast<uint64_t>(entry);
    thread.context->cs = current_code_selector();
    thread.context->rflags = 0x202;
    thread.context->rsp = stack_top - sizeof(uint64_t);
    thread.context->ss = current_stack_selector();
    thread.rwl_recovery_active = 0;
    thread.rwl_recovery_rip = 0;
    thread.rwl_recovery_rsp = 0;
    return &thread;
}

arch::x86_64::InterruptFrame* Scheduler::on_timer(arch::x86_64::InterruptFrame* frame)
{
    if (current_index_ < count_) {
        Thread& current = threads_[current_index_];
        if (current.state == ThreadState::Running) {
            current.context = frame;
            current.state = ThreadState::Ready;
            current.rwl_recovery_active = rawline_rwl_recovery_active;
            current.rwl_recovery_rip = rawline_rwl_recovery_rip;
            current.rwl_recovery_rsp = rawline_rwl_recovery_rsp;
        }
    }

    bool high_ready = false;
    bool normal_ready = false;
    for (size_t i = 0; i < count_; ++i) {
        if (threads_[i].state != ThreadState::Ready) continue;
        if (threads_[i].high_priority) high_ready = true;
        else normal_ready = true;
    }
    const bool choose_high = high_ready && (!normal_ready || high_priority_budget_ != 0);
    size_t& next_in_group = choose_high ? next_high_index_ : next_normal_index_;
    for (size_t attempt = 0; attempt < count_; ++attempt) {
        const size_t index = (next_in_group + attempt) % count_;
        Thread& candidate = threads_[index];
        if (candidate.state != ThreadState::Ready || candidate.high_priority != choose_high) continue;

        candidate.state = ThreadState::Running;
        current_index_ = index;
        next_in_group = (index + 1) % count_;
        if (choose_high && high_priority_budget_ != 0) --high_priority_budget_;
        else if (!choose_high) high_priority_budget_ = 4;
        rawline_rwl_recovery_active = candidate.rwl_recovery_active;
        rawline_rwl_recovery_rip = candidate.rwl_recovery_rip;
        rawline_rwl_recovery_rsp = candidate.rwl_recovery_rsp;
        return candidate.context;
    }

    current_index_ = maximum_threads;
    return frame;
}

}
