#pragma once

#include <stdint.h>

namespace rawline::wall_clock {

struct Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t reserved;
};

using Read = bool (*)(Time* time);

}
