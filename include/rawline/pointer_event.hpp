#pragma once

#include <stdint.h>

namespace rawline::pointer {

inline constexpr uint64_t event_version = 1;
inline constexpr uint32_t button_left = 1;

struct Event {
    uint64_t version;
    uint64_t size;
    int32_t delta_x;
    int32_t delta_y;
    uint32_t buttons;
    uint32_t reserved;
};

using Handler = void (*)(const Event* event);

}
