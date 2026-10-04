#pragma once

#include <stddef.h>
#include <stdint.h>

#include <rawline/kernel/process/process.hpp>

namespace rawline::kernel::process {

class ProcessManager {
public:
    static constexpr size_t maximum_processes = 64;

    ProcessManager();

    Process* create(const char* name, ProcessId parent_pid = 0);
    Process* find(ProcessId pid);
    const Process* find(ProcessId pid) const;
    bool terminate(ProcessId pid, int64_t exit_code);
    size_t enumerate(Process* output, size_t capacity) const;

private:
    ProcessId allocate_pid();

    Process processes_[maximum_processes];
    bool occupied_[maximum_processes];
    ProcessId next_pid_;
};

}
