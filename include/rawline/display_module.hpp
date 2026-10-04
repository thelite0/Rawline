#pragma once

#include <stdint.h>

#include <rawline/pointer_event.hpp>
#include <rawline/compositor_client.hpp>
#include <rawline/graphics_backend.hpp>

namespace rawline::display_module {

inline constexpr uint64_t api_version = 7;
inline constexpr uint64_t context_version = 3;

struct Api {
    uint64_t version;
    uint64_t size;
    void (*write_serial)(const char* text);
    bool (*register_pointer_handler)(pointer::Handler handler);
    bool (*create_thread)(void (*entry)(void*), void* context);
    uint64_t (*monotonic_ns)();
    void (*wait_for_interrupt)();
    uint64_t (*scheduler_ticks)();
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
    const compositor_client::Api** client_api;
    const graphics_backend::Api* graphics;
};

using EntryPoint = int64_t (*)(const Context* context);

}
