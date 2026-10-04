#include <rawline/kernel/process/process_manager.hpp>

namespace rawline::kernel::process {

ProcessManager::ProcessManager()
    : next_pid_(1)
{
    for (size_t i = 0; i < maximum_processes; ++i) {
        processes_[i] = Process{};
        occupied_[i] = false;
    }
}

ProcessId ProcessManager::allocate_pid()
{
    for (size_t attempt = 0; attempt <= maximum_processes; ++attempt) {
        ProcessId candidate = next_pid_++;
        if (candidate == 0) candidate = next_pid_++;
        if (find(candidate) == nullptr) return candidate;
    }
    return 0;
}

Process* ProcessManager::create(const char* name, ProcessId parent_pid)
{
    if (name == nullptr || (parent_pid != 0 && find(parent_pid) == nullptr)) return nullptr;

    size_t slot = 0;
    while (slot < maximum_processes && occupied_[slot]) ++slot;
    if (slot == maximum_processes) return nullptr;

    const ProcessId pid = allocate_pid();
    if (pid == 0) return nullptr;

    processes_[slot] = Process{
        .pid = pid,
        .name = name,
        .state = ProcessState::Created,
        .parent_pid = parent_pid,
        .exit_code = 0
    };
    occupied_[slot] = true;
    return &processes_[slot];
}

Process* ProcessManager::find(ProcessId pid)
{
    for (size_t i = 0; i < maximum_processes; ++i) {
        if (occupied_[i] && processes_[i].pid == pid) return &processes_[i];
    }
    return nullptr;
}

const Process* ProcessManager::find(ProcessId pid) const
{
    for (size_t i = 0; i < maximum_processes; ++i) {
        if (occupied_[i] && processes_[i].pid == pid) return &processes_[i];
    }
    return nullptr;
}

bool ProcessManager::terminate(ProcessId pid, int64_t exit_code)
{
    Process* process = find(pid);
    if (process == nullptr || process->state == ProcessState::Exited) return false;
    process->state = ProcessState::Exited;
    process->exit_code = exit_code;
    return true;
}

size_t ProcessManager::enumerate(Process* output, size_t capacity) const
{
    size_t count = 0;
    for (size_t i = 0; i < maximum_processes; ++i) {
        if (!occupied_[i]) continue;
        if (output != nullptr && count < capacity) output[count] = processes_[i];
        ++count;
    }
    return count;
}

}
