#pragma once

#include <stdint.h>

#include <rawline/pointer_event.hpp>

namespace rawline::input_module {

inline constexpr uint64_t api_version = 2;
inline constexpr uint64_t context_version = 1;

struct Api {
    uint64_t version;
    uint64_t size;
    bool (*poll_pointer_event)(pointer::Event* event);
    bool (*dispatch_pointer_event)(const pointer::Event* event);
    bool (*create_thread)(void (*entry)(void*), void* context);
    void (*write_serial)(const char* text);
    void (*wait_for_interrupt)();
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
};

using EntryPoint = int64_t (*)(const Context* context);

}
