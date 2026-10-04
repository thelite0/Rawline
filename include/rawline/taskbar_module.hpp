#pragma once

#include <stdint.h>

#include <rawline/compositor_client.hpp>
#include <rawline/wall_clock.hpp>

namespace rawline::taskbar_module {

inline constexpr uint64_t context_version = 1;

struct Context {
    uint64_t version;
    uint64_t size;
    const compositor_client::Api* compositor;
    wall_clock::Read read_wall_clock;
};

using EntryPoint = int64_t (*)(const Context* context);

}
