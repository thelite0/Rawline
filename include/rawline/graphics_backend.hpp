#pragma once

#include <stdint.h>

namespace rawline::graphics_backend {

inline constexpr uint64_t api_version = 1;
inline constexpr uint64_t info_version = 1;
inline constexpr uint32_t backend_software_framebuffer = 1;
inline constexpr uint32_t backend_virtio_gpu_2d = 2;
inline constexpr uint32_t format_rgbx8888 = 1;

struct Info {
    uint64_t version;
    uint64_t width;
    uint64_t height;
    uint64_t pitch_pixels;
    uint32_t format;
    uint32_t backend;
    uint64_t capabilities;
};

struct Region {
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
};

// The compositor owns scene policy and submits complete, already-composed
// regions. Backends own scanout resources and transport details.
struct Api {
    uint64_t version;
    uint64_t size;
    bool (*get_info)(Info* info);
    bool (*begin_frame)();
    bool (*present_regions)(const Region* regions, uint64_t region_count,
        const uint32_t* pixels, uint64_t source_pitch_pixels);
    bool (*end_frame)();
};

}
