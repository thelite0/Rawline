#pragma once

#include <stdint.h>

namespace rawline::kernel::process {

using ProcessId = uint64_t;

enum class ProcessState : uint8_t {
    Created,
    Ready,
    Running,
    Blocked,
    Exited
};

struct Process {
    ProcessId pid;
    const char* name;
    ProcessState state;
    ProcessId parent_pid;
    int64_t exit_code;
};

}
