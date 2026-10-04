#pragma once

#include <stdint.h>

namespace rawline::compositor_client {

inline constexpr uint64_t api_version = 1;
inline constexpr uint64_t context_version = 1;
inline constexpr uint64_t pointer_event_version = 1;
inline constexpr uint32_t surface_flag_pointer_events = 1;

struct SurfaceHandle {
    uint32_t slot;
    uint32_t generation;
};

struct Rect {
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
};

struct PointerEvent {
    uint64_t version;
    uint64_t size;
    int64_t x;
    int64_t y;
    int64_t surface_x;
    int64_t surface_y;
    uint32_t buttons;
    uint32_t pressed;
    uint32_t released;
    uint32_t reserved;
};

using PointerHandler = void (*)(const PointerEvent* event, void* context);
using TimerHandler = void (*)(uint64_t monotonic_ns, void* context);

struct Api {
    uint64_t version;
    uint64_t size;
    bool (*get_display_size)(uint64_t* width, uint64_t* height);
    bool (*create_surface)(uint64_t width, uint64_t height, uint32_t flags, SurfaceHandle* handle);
    bool (*destroy_surface)(SurfaceHandle handle);
    bool (*set_surface_position)(SurfaceHandle handle, int64_t x, int64_t y);
    bool (*set_surface_z_order)(SurfaceHandle handle, int32_t z);
    bool (*set_surface_visible)(SurfaceHandle handle, bool visible);
    bool (*fill_surface_rect)(SurfaceHandle handle, Rect rect, uint32_t color);
    bool (*draw_surface_text)(SurfaceHandle handle, int64_t x, int64_t y,
        const char* text, uint32_t color);
    bool (*draw_surface_pixels)(SurfaceHandle handle, Rect rect,
        const uint32_t* pixels, uint64_t source_pitch_pixels);
    bool (*damage_surface)(SurfaceHandle handle, Rect rect);
    bool (*set_pointer_handler)(SurfaceHandle handle, PointerHandler handler, void* context);
    bool (*set_timer_handler)(SurfaceHandle handle, uint64_t interval_ns,
        TimerHandler handler, void* context);
};

struct Context {
    uint64_t version;
    uint64_t size;
    const Api* api;
};

using EntryPoint = int64_t (*)(const Context* context);

}
