#include <stdint.h>

#include <rawline/display_module.hpp>
#include <rawline/compositor_client.hpp>

namespace {

struct Rect {
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
};

struct Surface {
    Rect bounds;
    uint32_t color;
    uint32_t z_order;
};

constexpr uint8_t ascii_font[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5f, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7f, 0x14, 0x7f, 0x14},
    {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1c, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1c, 0x00},
    {0x14, 0x08, 0x3e, 0x08, 0x14}, {0x08, 0x08, 0x3e, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3e}, {0x7e, 0x11, 0x11, 0x11, 0x7e},
    {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a},
    {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f},
    {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7f, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7f, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7f, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7f}, {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7e, 0x09, 0x01, 0x02}, {0x0c, 0x52, 0x52, 0x52, 0x3e},
    {0x7f, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7d, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3d, 0x00}, {0x7f, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7f, 0x40, 0x00}, {0x7c, 0x04, 0x18, 0x04, 0x78},
    {0x7c, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7c, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7c},
    {0x7c, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3f, 0x44, 0x40, 0x20}, {0x3c, 0x40, 0x40, 0x20, 0x7c},
    {0x1c, 0x20, 0x40, 0x20, 0x1c}, {0x3c, 0x40, 0x30, 0x40, 0x3c},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0c, 0x50, 0x50, 0x50, 0x3c},
    {0x44, 0x64, 0x54, 0x4c, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7f, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x04, 0x08, 0x10, 0x08}
};
static_assert(sizeof(ascii_font) / sizeof(ascii_font[0]) == 95);

constexpr uint64_t glyph_width = 5;
constexpr uint64_t glyph_height = 7;
constexpr uint64_t glyph_scale = 2;
constexpr uint64_t glyph_advance = 12;
constexpr uint64_t cursor_width = 9;
constexpr uint64_t cursor_height = 12;
constexpr uint64_t maximum_damage_rects = 64;
constexpr uint64_t frame_interval_ns = 8333333;
constexpr uint64_t client_surface_slot_count = 8;
constexpr uint64_t client_surface_pixel_capacity = 512 * 1024;

struct ClientSurface {
    uint32_t generation;
    uint32_t flags;
    uint64_t width;
    uint64_t height;
    uint64_t pixel_offset;
    int64_t x;
    int64_t y;
    int32_t z_order;
    bool active;
    bool visible;
    rawline::compositor_client::PointerHandler pointer_handler;
    void* pointer_context;
    uint64_t timer_interval_ns;
    uint64_t next_timer_ns;
    rawline::compositor_client::TimerHandler timer_handler;
    void* timer_context;
};

struct Desktop {
    rawline::display_module::Api api;
    const rawline::graphics_backend::Api* graphics;
    rawline::graphics_backend::Info info;
    Surface background;
    Surface window;
    ClientSurface client_surfaces[client_surface_slot_count];
    uint32_t* client_pixels;
    uint32_t next_surface_generation;
    uint64_t client_surface_pixels_used;
    Rect damage[maximum_damage_rects];
    uint64_t damage_count;
    uint32_t* shadow;
    int64_t cursor_x;
    int64_t cursor_y;
    int64_t drag_offset_x;
    int64_t drag_offset_y;
    uint32_t buttons;
    bool dragging;
    bool cursor_logged;
    bool drag_logged;
    bool perf_has_activity;
    uint64_t update_count;
    uint64_t updates_since_report;
    uint64_t present_count;
    uint64_t packets_since_report;
    uint64_t presents_since_report;
    uint64_t present_regions_since_report;
    uint64_t dirty_rects_since_report;
    uint64_t dirty_rects_max;
    uint64_t dirty_pixels_since_report;
    uint64_t dirty_pixels_max;
    uint64_t redrawn_pixels_since_report;
    uint64_t redrawn_pixels_max;
    uint64_t compose_cycles_since_report;
    uint64_t compose_cycles_max;
    uint64_t present_cycles_since_report;
    uint64_t present_cycles_max;
    uint64_t bytes_copied_since_report;
    uint64_t pixels_copied_since_report;
    uint64_t last_frame_ns;
    uint64_t report_ns;
    uint64_t last_report_ticks;
    uint64_t timer_ticks_since_report;
};

Desktop desktop{};
extern "C" uint8_t __compositor_image_end;
alignas(16) uint32_t shadow_storage[3840 * 2160];
alignas(16) uint32_t client_surface_storage[client_surface_pixel_capacity];

struct PendingPointer {
    int64_t delta_x;
    int64_t delta_y;
    int64_t press_delta_x;
    int64_t press_delta_y;
    uint64_t packet_count;
    uint32_t buttons;
    bool left_released;
    bool left_pressed;
};

PendingPointer pending_pointer{};

uint64_t disable_interrupts()
{
    uint64_t flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void restore_interrupts(uint64_t flags)
{
    if ((flags & (uint64_t{1} << 9)) != 0) asm volatile("sti" : : : "memory");
}

uint64_t read_tsc()
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32) | low;
}

void write_decimal(uint64_t value)
{
    char digits[20];
    uint64_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        const char one[] = {digits[--count], '\0'};
        desktop.api.write_serial(one);
    }
}

void write_performance_summary(uint64_t elapsed_ns)
{
    desktop.api.write_serial("RAWLINE perf ms="); write_decimal(elapsed_ns / 1000000);
    desktop.api.write_serial(" input_packets="); write_decimal(desktop.packets_since_report);
    desktop.api.write_serial(" scheduler_ticks="); write_decimal(desktop.timer_ticks_since_report);
    desktop.api.write_serial(" compositor_updates="); write_decimal(desktop.updates_since_report);
    desktop.api.write_serial(" present_frames="); write_decimal(desktop.presents_since_report);
    desktop.api.write_serial(" present_regions="); write_decimal(desktop.present_regions_since_report);
    desktop.api.write_serial(" dirty_rects="); write_decimal(desktop.dirty_rects_since_report);
    desktop.api.write_serial("/"); write_decimal(desktop.dirty_rects_max);
    desktop.api.write_serial(" dirty_pixels="); write_decimal(desktop.dirty_pixels_since_report);
    desktop.api.write_serial("/"); write_decimal(desktop.dirty_pixels_max);
    desktop.api.write_serial(" redrawn="); write_decimal(desktop.redrawn_pixels_since_report);
    desktop.api.write_serial("/"); write_decimal(desktop.redrawn_pixels_max);
    desktop.api.write_serial(" compose_cycles="); write_decimal(desktop.compose_cycles_since_report);
    desktop.api.write_serial("/"); write_decimal(desktop.compose_cycles_max);
    desktop.api.write_serial(" present_copy_cycles="); write_decimal(desktop.present_cycles_since_report);
    desktop.api.write_serial("/"); write_decimal(desktop.present_cycles_max);
    desktop.api.write_serial(" copied_bytes="); write_decimal(desktop.bytes_copied_since_report);
    desktop.api.write_serial(" copied_pixels="); write_decimal(desktop.pixels_copied_since_report);
    desktop.api.write_serial("\r\n");
}

bool intersects(const Rect& left, const Rect& right, Rect* result)
{
    const int64_t left_right = left.x + static_cast<int64_t>(left.width);
    const int64_t left_bottom = left.y + static_cast<int64_t>(left.height);
    const int64_t right_right = right.x + static_cast<int64_t>(right.width);
    const int64_t right_bottom = right.y + static_cast<int64_t>(right.height);
    const int64_t x0 = left.x > right.x ? left.x : right.x;
    const int64_t y0 = left.y > right.y ? left.y : right.y;
    const int64_t x1 = left_right < right_right ? left_right : right_right;
    const int64_t y1 = left_bottom < right_bottom ? left_bottom : right_bottom;
    if (x0 >= x1 || y0 >= y1) return false;
    *result = {x0, y0, static_cast<uint64_t>(x1 - x0), static_cast<uint64_t>(y1 - y0)};
    return true;
}

void fill_shadow_rect(const Rect& rect, uint32_t color)
{
    for (uint64_t row = 0; row < rect.height; ++row) {
        uint32_t* output = desktop.shadow +
            static_cast<uint64_t>(rect.y + static_cast<int64_t>(row)) * desktop.info.width + rect.x;
        uint64_t count = rect.width;
        asm volatile("cld; rep stosl"
            : "+D"(output), "+c"(count)
            : "a"(color)
            : "memory", "cc");
    }
}

bool draw_text(const Surface& surface,
    const Rect* damage, uint64_t damage_count, int64_t x, int64_t y,
    const char* text, uint32_t color)
{
    if (text == nullptr) return false;
    uint64_t character = 0;
    for (const char* current = text; *current != '\0'; ++current, ++character) {
        if (*current == '\n') {
            character = UINT64_MAX;
            y += static_cast<int64_t>(glyph_height * glyph_scale + 2);
            continue;
        }
        const uint8_t code = static_cast<uint8_t>(*current);
        const uint8_t* glyph = ascii_font[(code >= 32 && code <= 126) ? code - 32 : '?' - 32];
        const int64_t glyph_x = x + static_cast<int64_t>(character * glyph_advance);
        const Rect glyph_bounds{glyph_x, y, glyph_width * glyph_scale, glyph_height * glyph_scale};
        Rect clipped_glyph{};
        if (!intersects(glyph_bounds, surface.bounds, &clipped_glyph)) continue;
        bool glyph_damaged = false;
        for (uint64_t damage_index = 0; damage_index < damage_count; ++damage_index) {
            Rect ignored{};
            if (intersects(clipped_glyph, damage[damage_index], &ignored)) {
                glyph_damaged = true;
                break;
            }
        }
        if (!glyph_damaged) continue;
        for (uint64_t row = 0; row < glyph_height; ++row) {
            uint64_t column = 0;
            while (column < glyph_width) {
                while (column < glyph_width && (glyph[column] & (uint8_t{1} << row)) == 0) ++column;
                const uint64_t run_start = column;
                while (column < glyph_width && (glyph[column] & (uint8_t{1} << row)) != 0) ++column;
                if (run_start == column) continue;
                const Rect run{glyph_x + static_cast<int64_t>(run_start * glyph_scale),
                    y + static_cast<int64_t>(row * glyph_scale), (column - run_start) * glyph_scale,
                    glyph_scale};
                Rect clipped_run{};
                if (!intersects(run, surface.bounds, &clipped_run)) continue;
                for (uint64_t damage_index = 0; damage_index < damage_count; ++damage_index) {
                    Rect damaged{};
                    if (intersects(clipped_run, damage[damage_index], &damaged))
                        fill_shadow_rect(damaged, color);
                }
            }
        }
    }
    return true;
}

Rect bounds(const Surface& surface)
{
    return surface.bounds;
}

void add_damage(const Rect& requested)
{
    const Rect screen{0, 0, desktop.info.width, desktop.info.height};
    Rect clipped{};
    if (!intersects(requested, screen, &clipped)) return;
    // Merge overlapping or nearby regions when the bounding box adds at most
    // 25% work. This collapses small old/new drag overlaps to one recomposition
    // while keeping distant endpoints separate (never the travel path).
    for (uint64_t i = 0; i < desktop.damage_count;) {
        const Rect existing = desktop.damage[i];
        const int64_t x0 = existing.x < clipped.x ? existing.x : clipped.x;
        const int64_t y0 = existing.y < clipped.y ? existing.y : clipped.y;
        const int64_t x1 = existing.x + static_cast<int64_t>(existing.width) >
                clipped.x + static_cast<int64_t>(clipped.width)
            ? existing.x + static_cast<int64_t>(existing.width)
            : clipped.x + static_cast<int64_t>(clipped.width);
        const int64_t y1 = existing.y + static_cast<int64_t>(existing.height) >
                clipped.y + static_cast<int64_t>(clipped.height)
            ? existing.y + static_cast<int64_t>(existing.height)
            : clipped.y + static_cast<int64_t>(clipped.height);
        const uint64_t area = static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0);
        const uint64_t covered = existing.width * existing.height + clipped.width * clipped.height;
        const int64_t overlap_x0 = existing.x > clipped.x ? existing.x : clipped.x;
        const int64_t overlap_y0 = existing.y > clipped.y ? existing.y : clipped.y;
        const int64_t overlap_x1 = existing.x + static_cast<int64_t>(existing.width) <
                clipped.x + static_cast<int64_t>(clipped.width)
            ? existing.x + static_cast<int64_t>(existing.width)
            : clipped.x + static_cast<int64_t>(clipped.width);
        const int64_t overlap_y1 = existing.y + static_cast<int64_t>(existing.height) <
                clipped.y + static_cast<int64_t>(clipped.height)
            ? existing.y + static_cast<int64_t>(existing.height)
            : clipped.y + static_cast<int64_t>(clipped.height);
        const bool overlaps = overlap_x0 < overlap_x1 && overlap_y0 < overlap_y1;
        if (overlaps || area <= covered + covered / 4) {
            clipped = {x0, y0, static_cast<uint64_t>(x1 - x0), static_cast<uint64_t>(y1 - y0)};
            desktop.damage[i] = desktop.damage[--desktop.damage_count];
            i = 0;
            continue;
        }
        ++i;
    }
    if (desktop.damage_count == maximum_damage_rects) {
        // Merge the pair with the least added area; never promote queue pressure
        // to full-screen damage. New motion damage is already coalesced per frame.
        uint64_t best_cost = UINT64_MAX;
        uint64_t best_i = 0, best_j = 1;
        for (uint64_t i = 0; i < desktop.damage_count; ++i) {
            for (uint64_t j = i + 1; j < desktop.damage_count; ++j) {
                const Rect& a = desktop.damage[i];
                const Rect& b = desktop.damage[j];
                const int64_t x0 = a.x < b.x ? a.x : b.x;
                const int64_t y0 = a.y < b.y ? a.y : b.y;
                const int64_t x1 = a.x + static_cast<int64_t>(a.width) > b.x + static_cast<int64_t>(b.width)
                    ? a.x + static_cast<int64_t>(a.width) : b.x + static_cast<int64_t>(b.width);
                const int64_t y1 = a.y + static_cast<int64_t>(a.height) > b.y + static_cast<int64_t>(b.height)
                    ? a.y + static_cast<int64_t>(a.height) : b.y + static_cast<int64_t>(b.height);
                const uint64_t area = static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0);
                const uint64_t covered = a.width * a.height + b.width * b.height;
                const uint64_t cost = area > covered ? area - covered : 0;
                if (cost < best_cost) { best_cost = cost; best_i = i; best_j = j; }
            }
        }
        const Rect a = desktop.damage[best_i], b = desktop.damage[best_j];
        const int64_t x0 = a.x < b.x ? a.x : b.x;
        const int64_t y0 = a.y < b.y ? a.y : b.y;
        const int64_t x1 = a.x + static_cast<int64_t>(a.width) > b.x + static_cast<int64_t>(b.width)
            ? a.x + static_cast<int64_t>(a.width) : b.x + static_cast<int64_t>(b.width);
        const int64_t y1 = a.y + static_cast<int64_t>(a.height) > b.y + static_cast<int64_t>(b.height)
            ? a.y + static_cast<int64_t>(a.height) : b.y + static_cast<int64_t>(b.height);
        desktop.damage[best_i] = {x0, y0, static_cast<uint64_t>(x1 - x0), static_cast<uint64_t>(y1 - y0)};
        desktop.damage[best_j] = desktop.damage[--desktop.damage_count];
        add_damage(clipped);
        return;
    }
    desktop.damage[desktop.damage_count++] = clipped;
}

bool point_in_rect(int64_t x, int64_t y, const Rect& rect)
{
    return x >= rect.x && y >= rect.y &&
        x < rect.x + static_cast<int64_t>(rect.width) &&
        y < rect.y + static_cast<int64_t>(rect.height);
}

ClientSurface* get_client_surface(rawline::compositor_client::SurfaceHandle handle)
{
    if (handle.slot >= client_surface_slot_count) return nullptr;
    ClientSurface& surface = desktop.client_surfaces[handle.slot];
    return surface.active && surface.generation == handle.generation ? &surface : nullptr;
}

Rect client_surface_bounds(const ClientSurface& surface)
{
    return {surface.x, surface.y, surface.width, surface.height};
}

void damage_client_rect(ClientSurface& surface, rawline::compositor_client::Rect rect)
{
    const Rect local{0, 0, surface.width, surface.height};
    const Rect requested{rect.x, rect.y, rect.width, rect.height};
    Rect clipped{};
    if (!intersects(local, requested, &clipped)) return;
    add_damage({surface.x + clipped.x, surface.y + clipped.y, clipped.width, clipped.height});
}

bool client_get_display_size(uint64_t* width, uint64_t* height)
{
    if (width == nullptr || height == nullptr || desktop.info.width == 0) return false;
    *width = desktop.info.width;
    *height = desktop.info.height;
    return true;
}

bool client_create_surface(uint64_t width, uint64_t height, uint32_t flags,
    rawline::compositor_client::SurfaceHandle* handle)
{
    if (handle == nullptr || width == 0 || height == 0 || width > desktop.info.width ||
        height > desktop.info.height || (flags & ~rawline::compositor_client::surface_flag_pointer_events) != 0 ||
        width > UINT64_MAX / height) return false;
    const uint64_t pixel_count = width * height;
    if (pixel_count > client_surface_pixel_capacity) return false;
    uint64_t candidate = 0;
    bool found = false;
    while (candidate <= client_surface_pixel_capacity - pixel_count) {
        uint64_t next_candidate = candidate;
        bool collision = false;
        for (const ClientSurface& active : desktop.client_surfaces) {
            if (!active.active) continue;
            const uint64_t active_count = active.width * active.height;
            const uint64_t active_end = active.pixel_offset + active_count;
            if (candidate < active_end && candidate + pixel_count > active.pixel_offset) {
                next_candidate = active_end;
                collision = true;
                break;
            }
        }
        if (!collision) { found = true; break; }
        candidate = next_candidate;
    }
    if (!found) return false;
    for (uint32_t slot = 0; slot < client_surface_slot_count; ++slot) {
        ClientSurface& surface = desktop.client_surfaces[slot];
        if (surface.active) continue;
        uint32_t generation = ++desktop.next_surface_generation;
        if (generation == 0) generation = ++desktop.next_surface_generation;
        surface = {generation, flags, width, height, candidate, 0, 0, 100, true, false,
            nullptr, nullptr, 0, 0, nullptr, nullptr};
        for (uint64_t pixel = 0; pixel < pixel_count; ++pixel)
            desktop.client_pixels[candidate + pixel] = 0;
        *handle = {slot, generation};
        return true;
    }
    return false;
}

bool client_destroy_surface(rawline::compositor_client::SurfaceHandle handle)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    if (surface->visible) add_damage(client_surface_bounds(*surface));
    surface->active = false;
    surface->visible = false;
    ++surface->generation;
    if (surface->generation == 0) ++surface->generation;
    surface->pointer_handler = nullptr;
    surface->timer_handler = nullptr;
    return true;
}

bool client_set_surface_position(rawline::compositor_client::SurfaceHandle handle, int64_t x, int64_t y)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    if (surface->visible) add_damage(client_surface_bounds(*surface));
    surface->x = x;
    surface->y = y;
    if (surface->visible) add_damage(client_surface_bounds(*surface));
    return true;
}

bool client_set_surface_z_order(rawline::compositor_client::SurfaceHandle handle, int32_t z_order)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    if (surface->visible) add_damage(client_surface_bounds(*surface));
    surface->z_order = z_order;
    if (surface->visible) add_damage(client_surface_bounds(*surface));
    return true;
}

bool client_set_surface_visible(rawline::compositor_client::SurfaceHandle handle, bool visible)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    if (surface->visible == visible) return true;
    surface->visible = visible;
    add_damage(client_surface_bounds(*surface));
    return true;
}

bool client_fill_surface_rect(rawline::compositor_client::SurfaceHandle handle,
    rawline::compositor_client::Rect rect, uint32_t color)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    Rect clipped{};
    if (!intersects({0, 0, surface->width, surface->height},
        {rect.x, rect.y, rect.width, rect.height}, &clipped)) return true;
    for (uint64_t row = 0; row < clipped.height; ++row) {
        uint32_t* output = desktop.client_pixels + surface->pixel_offset +
            static_cast<uint64_t>(clipped.y + static_cast<int64_t>(row)) * surface->width + clipped.x;
        uint64_t count = clipped.width;
        asm volatile("cld; rep stosl" : "+D"(output), "+c"(count) : "a"(color) : "memory", "cc");
    }
    damage_client_rect(*surface, rect);
    return true;
}

bool client_draw_surface_text(rawline::compositor_client::SurfaceHandle handle,
    int64_t x, int64_t y, const char* text, uint32_t color)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr || text == nullptr || x < 0 || y < 0) return false;
    uint64_t length = 0;
    while (length < 128 && text[length] != '\0') ++length;
    if (length == 128 && text[length] != '\0') return false;
    const uint64_t text_width = length * glyph_advance;
    for (uint64_t character = 0; character < length; ++character) {
        const uint8_t code = static_cast<uint8_t>(text[character]);
        if (code == '\n') continue;
        const uint8_t* glyph = ascii_font[(code >= 32 && code <= 126) ? code - 32 : '?' - 32];
        for (uint64_t column = 0; column < glyph_width; ++column) {
            for (uint64_t row = 0; row < glyph_height; ++row) {
                if ((glyph[column] & (uint8_t{1} << row)) == 0) continue;
                const uint64_t px = static_cast<uint64_t>(x) + character * glyph_advance + column * glyph_scale;
                const uint64_t py = static_cast<uint64_t>(y) + row * glyph_scale;
                if (px >= surface->width || py >= surface->height) continue;
                const uint64_t block_width = px + glyph_scale <= surface->width ? glyph_scale : surface->width - px;
                const uint64_t block_height = py + glyph_scale <= surface->height ? glyph_scale : surface->height - py;
                for (uint64_t dy = 0; dy < block_height; ++dy)
                    for (uint64_t dx = 0; dx < block_width; ++dx)
                        desktop.client_pixels[surface->pixel_offset + (py + dy) * surface->width + px + dx] = color;
            }
        }
    }
    damage_client_rect(*surface, {x, y, text_width, glyph_height * glyph_scale});
    return true;
}

bool client_draw_surface_pixels(rawline::compositor_client::SurfaceHandle handle,
    rawline::compositor_client::Rect rect, const uint32_t* pixels, uint64_t source_pitch_pixels)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr || pixels == nullptr || source_pitch_pixels < rect.width || rect.x < 0 || rect.y < 0)
        return false;
    Rect clipped{};
    if (!intersects({0, 0, surface->width, surface->height},
        {rect.x, rect.y, rect.width, rect.height}, &clipped)) return true;
    const uint64_t source_x = static_cast<uint64_t>(clipped.x - rect.x);
    const uint64_t source_y = static_cast<uint64_t>(clipped.y - rect.y);
    for (uint64_t row = 0; row < clipped.height; ++row) {
        const uint32_t* input = pixels + (source_y + row) * source_pitch_pixels + source_x;
        uint32_t* output = desktop.client_pixels + surface->pixel_offset +
            static_cast<uint64_t>(clipped.y + static_cast<int64_t>(row)) * surface->width + clipped.x;
        for (uint64_t column = 0; column < clipped.width; ++column) output[column] = input[column];
    }
    damage_client_rect(*surface, rect);
    return true;
}

bool client_damage_surface(rawline::compositor_client::SurfaceHandle handle,
    rawline::compositor_client::Rect rect)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr) return false;
    if (surface->visible) damage_client_rect(*surface, rect);
    return true;
}

bool client_set_pointer_handler(rawline::compositor_client::SurfaceHandle handle,
    rawline::compositor_client::PointerHandler handler, void* context)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr || ((surface->flags & rawline::compositor_client::surface_flag_pointer_events) != 0 && handler == nullptr))
        return false;
    surface->pointer_handler = handler;
    surface->pointer_context = context;
    return true;
}

bool client_set_timer_handler(rawline::compositor_client::SurfaceHandle handle, uint64_t interval_ns,
    rawline::compositor_client::TimerHandler handler, void* context)
{
    ClientSurface* surface = get_client_surface(handle);
    if (surface == nullptr || (handler != nullptr && interval_ns == 0)) return false;
    surface->timer_interval_ns = interval_ns;
    surface->next_timer_ns = desktop.api.monotonic_ns() + interval_ns;
    surface->timer_handler = handler;
    surface->timer_context = context;
    return true;
}

const rawline::compositor_client::Api compositor_client_api{
    rawline::compositor_client::api_version,
    sizeof(rawline::compositor_client::Api),
    client_get_display_size,
    client_create_surface,
    client_destroy_surface,
    client_set_surface_position,
    client_set_surface_z_order,
    client_set_surface_visible,
    client_fill_surface_rect,
    client_draw_surface_text,
    client_draw_surface_pixels,
    client_damage_surface,
    client_set_pointer_handler,
    client_set_timer_handler
};

bool pixel_damaged(int64_t x, int64_t y, const Rect* damage, uint64_t damage_count)
{
    for (uint64_t i = 0; i < damage_count; ++i)
        if (point_in_rect(x, y, damage[i])) return true;
    return false;
}

void add_window_move_damage(const Rect& old_bounds, const Rect& new_bounds)
{
    // Damage the two final window positions independently. The shadow buffer
    // is recomposed for each complete endpoint, while the path between them
    // remains untouched regardless of pointer travel distance.
    add_damage(old_bounds);
    add_damage(new_bounds);
}

bool draw_cursor(const Rect* damage, uint64_t damage_count)
{
    static constexpr uint16_t shape[cursor_height] = {
        0x001, 0x003, 0x007, 0x00f, 0x01f, 0x03f,
        0x07f, 0x0ff, 0x03f, 0x01f, 0x01b, 0x031
    };
    const Rect cursor_bounds{desktop.cursor_x, desktop.cursor_y, cursor_width, cursor_height};
    bool cursor_damaged = false;
    for (uint64_t index = 0; index < damage_count; ++index) {
        Rect ignored{};
        if (intersects(cursor_bounds, damage[index], &ignored)) { cursor_damaged = true; break; }
    }
    if (!cursor_damaged) return true;
    for (uint64_t y = 0; y < cursor_height; ++y) {
        for (uint64_t x = 0; x < cursor_width; ++x) {
            if ((shape[y] & (uint16_t{1} << x)) == 0) continue;
            const int64_t screen_x = desktop.cursor_x + static_cast<int64_t>(x);
            const int64_t screen_y = desktop.cursor_y + static_cast<int64_t>(y);
            if (!pixel_damaged(screen_x, screen_y, damage, damage_count)) continue;
            const bool interior = y >= 2 && y <= 6 && x > 0 && x < y;
            desktop.shadow[static_cast<uint64_t>(screen_y) * desktop.info.width + screen_x] =
                interior ? 0xffffff : 0x101820;
        }
    }
    return true;
}

uint64_t dirty_pixel_count(const Rect* damage, uint64_t count)
{
    uint64_t pixels = 0;
    for (uint64_t i = 0; i < count; ++i) pixels += damage[i].width * damage[i].height;
    return pixels;
}

void draw_client_surface(const ClientSurface& surface, const Rect& dirty)
{
    if (!surface.active || !surface.visible) return;
    Rect clipped{};
    if (!intersects(client_surface_bounds(surface), dirty, &clipped)) return;
    const uint64_t source_x = static_cast<uint64_t>(clipped.x - surface.x);
    const uint64_t source_y = static_cast<uint64_t>(clipped.y - surface.y);
    for (uint64_t row = 0; row < clipped.height; ++row) {
        const uint32_t* input = desktop.client_pixels + surface.pixel_offset +
            (source_y + row) * surface.width + source_x;
        uint32_t* output = desktop.shadow +
            static_cast<uint64_t>(clipped.y + static_cast<int64_t>(row)) * desktop.info.width + clipped.x;
        for (uint64_t column = 0; column < clipped.width; ++column) output[column] = input[column];
    }
}

void compose_client_surfaces(const Rect& dirty)
{
    bool any_visible_intersection = false;
    for (const ClientSurface& surface : desktop.client_surfaces) {
        if (!surface.active || !surface.visible) continue;
        Rect ignored{};
        if (intersects(client_surface_bounds(surface), dirty, &ignored)) {
            any_visible_intersection = true;
            break;
        }
    }
    if (!any_visible_intersection) return;
    uint32_t order[client_surface_slot_count];
    for (uint32_t i = 0; i < client_surface_slot_count; ++i) order[i] = i;
    for (uint32_t i = 0; i < client_surface_slot_count; ++i) {
        for (uint32_t j = i + 1; j < client_surface_slot_count; ++j) {
            const ClientSurface& a = desktop.client_surfaces[order[i]];
            const ClientSurface& b = desktop.client_surfaces[order[j]];
            if (b.active && (!a.active || b.z_order < a.z_order ||
                (b.z_order == a.z_order && order[j] < order[i]))) {
                const uint32_t swap = order[i]; order[i] = order[j]; order[j] = swap;
            }
        }
    }
    for (uint32_t i = 0; i < client_surface_slot_count; ++i)
        draw_client_surface(desktop.client_surfaces[order[i]], dirty);
}

bool render_damage()
{
    const uint64_t damage_count = desktop.damage_count;
    const uint64_t dirty_pixels = dirty_pixel_count(desktop.damage, damage_count);
    const uint64_t started = read_tsc();
    for (uint64_t i = 0; i < damage_count; ++i) {
        const Rect dirty = desktop.damage[i];
        const Rect clipped{dirty.x, dirty.y, dirty.width, dirty.height};
        fill_shadow_rect(clipped, desktop.background.color);
        if (!draw_text(desktop.background, &dirty, 1, 24, 32,
                "display ready", 0xd4e0eb)) return false;
        Rect window_clip{};
        if (intersects(desktop.window.bounds, dirty, &window_clip)) {
            fill_shadow_rect(window_clip, desktop.window.color);
        }
        if (desktop.window.bounds.width > 24 && desktop.window.bounds.height > 44) {
            const Rect header{desktop.window.bounds.x, desktop.window.bounds.y,
                desktop.window.bounds.width, 36};
            Rect clipped{};
            if (intersects(header, dirty, &clipped))
                fill_shadow_rect(clipped, 0x354b63);
            const Rect body{desktop.window.bounds.x, desktop.window.bounds.y + 40,
                desktop.window.bounds.width, desktop.window.bounds.height - 48};
            if (intersects(body, dirty, &clipped))
                fill_shadow_rect(clipped, 0x26394d);
            if (!draw_text(desktop.window, &dirty, 1,
                desktop.window.bounds.x + 14, desktop.window.bounds.y + 13,
                "Rawline", 0xf5f8fc)) return false;
        }
        compose_client_surfaces(dirty);
    }
    if (!draw_cursor(desktop.damage, damage_count)) return false;
    const uint64_t compose_cycles = read_tsc() - started;
    const uint64_t present_started = read_tsc();
    if (!desktop.graphics->begin_frame()) return false;
    rawline::graphics_backend::Region regions[maximum_damage_rects]{};
    for (uint64_t i = 0; i < damage_count; ++i) {
        const Rect dirty = desktop.damage[i];
        regions[i] = {dirty.x, dirty.y, dirty.width, dirty.height};
    }
    if (!desktop.graphics->present_regions(regions, damage_count, desktop.shadow, desktop.info.width))
        return false;
    if (!desktop.graphics->end_frame()) return false;
    const uint64_t present_cycles = read_tsc() - present_started;
    desktop.damage_count = 0;
    const uint64_t cycles = compose_cycles;
    const uint64_t redrawn = dirty_pixels;
    ++desktop.present_count;
    ++desktop.presents_since_report;
    desktop.present_regions_since_report += damage_count;
    desktop.dirty_rects_since_report += damage_count;
    if (desktop.dirty_rects_max < damage_count) desktop.dirty_rects_max = damage_count;
    desktop.dirty_pixels_since_report += dirty_pixels;
    if (desktop.dirty_pixels_max < dirty_pixels) desktop.dirty_pixels_max = dirty_pixels;
    desktop.redrawn_pixels_since_report += dirty_pixels;
    desktop.redrawn_pixels_max = desktop.redrawn_pixels_max < redrawn ? redrawn : desktop.redrawn_pixels_max;
    desktop.compose_cycles_since_report += cycles;
    desktop.compose_cycles_max = desktop.compose_cycles_max < cycles ? cycles : desktop.compose_cycles_max;
    desktop.present_cycles_since_report += present_cycles;
    desktop.present_cycles_max = desktop.present_cycles_max < present_cycles ? present_cycles : desktop.present_cycles_max;
    desktop.pixels_copied_since_report += dirty_pixels;
    desktop.bytes_copied_since_report += dirty_pixels * sizeof(uint32_t);
    return true;
}

void dispatch_surface_timers(uint64_t now)
{
    for (ClientSurface& surface : desktop.client_surfaces) {
        if (!surface.active || surface.timer_handler == nullptr || now < surface.next_timer_ns) continue;
        surface.next_timer_ns = now + surface.timer_interval_ns;
        surface.timer_handler(now, surface.timer_context);
    }
}

void process_pointer_update(const PendingPointer& pending)
{
    desktop.packets_since_report += pending.packet_count;
    desktop.perf_has_activity = desktop.perf_has_activity || pending.packet_count != 0;
    const int64_t old_cursor_x = desktop.cursor_x;
    const int64_t old_cursor_y = desktop.cursor_y;
    desktop.cursor_x += pending.delta_x;
    desktop.cursor_y += pending.delta_y;
    if (desktop.cursor_x < 0) desktop.cursor_x = 0;
    if (desktop.cursor_y < 0) desktop.cursor_y = 0;
    if (desktop.cursor_x >= static_cast<int64_t>(desktop.info.width))
        desktop.cursor_x = static_cast<int64_t>(desktop.info.width - 1);
    if (desktop.cursor_y >= static_cast<int64_t>(desktop.info.height))
        desktop.cursor_y = static_cast<int64_t>(desktop.info.height - 1);
    const bool cursor_moved = old_cursor_x != desktop.cursor_x || old_cursor_y != desktop.cursor_y;
    if (cursor_moved) {
        add_damage({old_cursor_x, old_cursor_y, cursor_width, cursor_height});
        add_damage({desktop.cursor_x, desktop.cursor_y, cursor_width, cursor_height});
    }

    const bool was_down = (desktop.buttons & rawline::pointer::button_left) != 0;
    const bool is_down = (pending.buttons & rawline::pointer::button_left) != 0;
    const Rect header{desktop.window.bounds.x, desktop.window.bounds.y,
        desktop.window.bounds.width, 40};
    const int64_t press_x = old_cursor_x + (pending.left_pressed ? pending.press_delta_x : pending.delta_x);
    const int64_t press_y = old_cursor_y + (pending.left_pressed ? pending.press_delta_y : pending.delta_y);
    bool surface_consumed_press = false;
    if (pending.left_pressed || pending.left_released) {
        const int64_t event_x = pending.left_pressed ? press_x : desktop.cursor_x;
        const int64_t event_y = pending.left_pressed ? press_y : desktop.cursor_y;
        ClientSurface* hit = nullptr;
        for (ClientSurface& surface : desktop.client_surfaces) {
            if (!surface.active || !surface.visible || surface.pointer_handler == nullptr ||
                (surface.flags & rawline::compositor_client::surface_flag_pointer_events) == 0 ||
                !point_in_rect(event_x, event_y, client_surface_bounds(surface))) continue;
            if (hit == nullptr || surface.z_order > hit->z_order) hit = &surface;
        }
        if (hit != nullptr) {
            const rawline::compositor_client::PointerEvent event{
                rawline::compositor_client::pointer_event_version,
                sizeof(rawline::compositor_client::PointerEvent),
                event_x, event_y, event_x - hit->x, event_y - hit->y,
                pending.buttons,
                pending.left_pressed ? rawline::pointer::button_left : 0,
                pending.left_released ? rawline::pointer::button_left : 0,
                0
            };
            hit->pointer_handler(&event, hit->pointer_context);
            surface_consumed_press = pending.left_pressed;
        }
    }
    if (!surface_consumed_press && ((!was_down && is_down) || pending.left_pressed) &&
        point_in_rect(press_x, press_y, header)) {
        desktop.dragging = true;
        desktop.drag_offset_x = press_x - desktop.window.bounds.x;
        desktop.drag_offset_y = press_y - desktop.window.bounds.y;
    }
    bool moved_window = false;
    if ((is_down || pending.left_released) && desktop.dragging && cursor_moved) {
        const Rect old_window = bounds(desktop.window);
        int64_t next_x = desktop.cursor_x - desktop.drag_offset_x;
        int64_t next_y = desktop.cursor_y - desktop.drag_offset_y;
        const int64_t max_x = static_cast<int64_t>(desktop.info.width - desktop.window.bounds.width);
        const int64_t max_y = static_cast<int64_t>(desktop.info.height - desktop.window.bounds.height);
        if (next_x < 0) next_x = 0;
        if (next_y < 0) next_y = 0;
        if (next_x > max_x) next_x = max_x;
        if (next_y > max_y) next_y = max_y;
        if (next_x != desktop.window.bounds.x || next_y != desktop.window.bounds.y) {
            desktop.window.bounds.x = next_x;
            desktop.window.bounds.y = next_y;
            add_window_move_damage(old_window, desktop.window.bounds);
            moved_window = true;
        }
    }
    if (((was_down && !is_down) || pending.left_released) && desktop.dragging) {
        desktop.dragging = false;
    }
    desktop.buttons = pending.buttons;
    if (!desktop.cursor_logged && cursor_moved) {
        desktop.cursor_logged = true;
        desktop.api.write_serial("RAWLINE compositor cursor moved\r\n");
    }
    if (moved_window && !desktop.drag_logged) {
        desktop.drag_logged = true;
        desktop.api.write_serial("RAWLINE compositor window moved with damage rectangles\r\n");
    }
    if (desktop.damage_count != 0 && !render_damage())
        desktop.api.write_serial("RAWLINE compositor damaged redraw failed\r\n");
}

void compositor_pointer_event(const rawline::pointer::Event* event)
{
    if (event == nullptr || event->version != rawline::pointer::event_version ||
        event->size < sizeof(rawline::pointer::Event)) return;
    const uint64_t flags = disable_interrupts();
    const bool was_down = pending_pointer.packet_count != 0
        ? (pending_pointer.buttons & rawline::pointer::button_left) != 0
        : (desktop.buttons & rawline::pointer::button_left) != 0;
    const bool is_down = (event->buttons & rawline::pointer::button_left) != 0;
    if (!was_down && is_down && !pending_pointer.left_pressed) {
        pending_pointer.press_delta_x = pending_pointer.delta_x + event->delta_x;
        pending_pointer.press_delta_y = pending_pointer.delta_y + event->delta_y;
        pending_pointer.left_pressed = true;
    }
    if (was_down && !is_down) pending_pointer.left_released = true;
    pending_pointer.delta_x += event->delta_x;
    pending_pointer.delta_y += event->delta_y;
    pending_pointer.buttons = event->buttons;
    ++pending_pointer.packet_count;
    restore_interrupts(flags);
}

void compositor_worker(void*)
{
    for (;;) {
        const uint64_t now = desktop.api.monotonic_ns();
        if (now - desktop.last_frame_ns < frame_interval_ns) {
            desktop.api.wait_for_interrupt();
            continue;
        }
        desktop.last_frame_ns = now;
        ++desktop.update_count;
        ++desktop.updates_since_report;
        PendingPointer pending{};
        const uint64_t flags = disable_interrupts();
        pending = pending_pointer;
        pending_pointer = {};
        restore_interrupts(flags);
        if (pending.packet_count != 0) {
            process_pointer_update(pending);
        }
        dispatch_surface_timers(now);
        if (desktop.damage_count != 0 && !render_damage())
            desktop.api.write_serial("RAWLINE compositor timer damage redraw failed\r\n");

        if (now - desktop.report_ns >= 1000000000) {
            const uint64_t current_ticks = desktop.api.scheduler_ticks();
            desktop.timer_ticks_since_report = current_ticks - desktop.last_report_ticks;
            desktop.last_report_ticks = current_ticks;
            if (desktop.perf_has_activity) write_performance_summary(now - desktop.report_ns);
            desktop.report_ns = now;
            desktop.perf_has_activity = false;
            desktop.packets_since_report = 0;
            desktop.updates_since_report = 0;
            desktop.presents_since_report = 0;
            desktop.present_regions_since_report = 0;
            desktop.dirty_rects_since_report = 0;
            desktop.dirty_rects_max = 0;
            desktop.dirty_pixels_since_report = 0;
            desktop.dirty_pixels_max = 0;
            desktop.redrawn_pixels_since_report = 0;
            desktop.redrawn_pixels_max = 0;
            desktop.compose_cycles_since_report = 0;
            desktop.compose_cycles_max = 0;
            desktop.present_cycles_since_report = 0;
            desktop.present_cycles_max = 0;
            desktop.bytes_copied_since_report = 0;
            desktop.pixels_copied_since_report = 0;
        }
    }
}

}

extern "C" int64_t compositor_entry(const rawline::display_module::Context* context)
{
    if (context == nullptr || context->version != rawline::display_module::context_version ||
        context->size < sizeof(rawline::display_module::Context) || context->api == nullptr ||
        context->api->version != rawline::display_module::api_version ||
        context->api->size < sizeof(rawline::display_module::Api) ||
        context->api->write_serial == nullptr ||
        context->api->register_pointer_handler == nullptr || context->api->create_thread == nullptr ||
        context->api->monotonic_ns == nullptr || context->api->wait_for_interrupt == nullptr ||
        context->api->scheduler_ticks == nullptr ||
        context->client_api == nullptr || context->graphics == nullptr ||
        context->graphics->version != rawline::graphics_backend::api_version ||
        context->graphics->size < sizeof(rawline::graphics_backend::Api) ||
        context->graphics->get_info == nullptr || context->graphics->begin_frame == nullptr ||
        context->graphics->present_regions == nullptr || context->graphics->end_frame == nullptr) return -1;

    rawline::graphics_backend::Info info{};
    if (!context->graphics->get_info(&info) ||
        info.version != rawline::graphics_backend::info_version ||
        info.width == 0 || info.height == 0 || info.width > INT64_MAX || info.height > INT64_MAX) return -1;

    desktop.api.version = context->api->version;
    desktop.api.size = context->api->size;
    desktop.graphics = context->graphics;
    desktop.api.write_serial = context->api->write_serial;
    desktop.api.register_pointer_handler = context->api->register_pointer_handler;
    desktop.api.create_thread = context->api->create_thread;
    desktop.api.monotonic_ns = context->api->monotonic_ns;
    desktop.api.wait_for_interrupt = context->api->wait_for_interrupt;
    desktop.api.scheduler_ticks = context->api->scheduler_ticks;
    desktop.client_pixels = client_surface_storage;
    desktop.next_surface_generation = 0;
    desktop.client_surface_pixels_used = 0;
    for (ClientSurface& surface : desktop.client_surfaces) surface = {};
    desktop.info.version = info.version;
    desktop.info.width = info.width;
    desktop.info.height = info.height;
    desktop.info.pitch_pixels = info.pitch_pixels;
    desktop.damage_count = 0;
    desktop.buttons = 0;
    desktop.dragging = false;
    desktop.cursor_logged = false;
    desktop.drag_logged = false;
    desktop.perf_has_activity = false;
    desktop.update_count = 0;
    desktop.updates_since_report = 0;
    desktop.present_count = 0;
    desktop.packets_since_report = 0;
    desktop.presents_since_report = 0;
    desktop.present_regions_since_report = 0;
    desktop.dirty_rects_since_report = 0;
    desktop.dirty_rects_max = 0;
    desktop.dirty_pixels_since_report = 0;
    desktop.dirty_pixels_max = 0;
    desktop.redrawn_pixels_since_report = 0;
    desktop.redrawn_pixels_max = 0;
    desktop.compose_cycles_since_report = 0;
    desktop.compose_cycles_max = 0;
    desktop.shadow = nullptr;
    const uint64_t pixel_count = info.width * info.height;
    if (pixel_count / info.width != info.height || pixel_count > 1920ull * 1080ull) return -1;
    desktop.shadow = shadow_storage;
    if (reinterpret_cast<uint64_t>(shadow_storage + pixel_count) >
        reinterpret_cast<uint64_t>(&__compositor_image_end)) return -1;
    pending_pointer = {};
    desktop.last_frame_ns = desktop.api.monotonic_ns();
    desktop.report_ns = desktop.last_frame_ns;
    desktop.last_report_ticks = desktop.api.scheduler_ticks();
    *context->client_api = &compositor_client_api;

    const uint64_t window_width = info.width < 320 ? info.width / 2 : 320;
    const uint64_t window_height = info.height < 220 ? info.height / 2 : 220;
    if (window_width == 0 || window_height == 0) return -1;
    desktop.background.bounds = {0, 0, info.width, info.height};
    desktop.background.color = 0x20354b;
    desktop.background.z_order = 0;
    desktop.window.bounds = {static_cast<int64_t>((info.width - window_width) / 2),
        static_cast<int64_t>((info.height - window_height) / 2), window_width, window_height};
    desktop.window.color = 0xd88923;
    desktop.window.z_order = 10;
    desktop.cursor_x = static_cast<int64_t>(info.width / 2);
    desktop.cursor_y = static_cast<int64_t>(info.height / 2);
    desktop.api.write_serial("RAWLINE compositor.rwl entered\r\n");
    if (!desktop.api.register_pointer_handler(compositor_pointer_event)) return -1;
    add_damage(desktop.background.bounds);
    if (!render_damage()) return -1;
    desktop.present_count = 0;
    desktop.presents_since_report = 0;
    desktop.present_regions_since_report = 0;
    desktop.dirty_rects_since_report = 0;
    desktop.dirty_rects_max = 0;
    desktop.dirty_pixels_since_report = 0;
    desktop.dirty_pixels_max = 0;
    desktop.redrawn_pixels_since_report = 0;
    desktop.redrawn_pixels_max = 0;
    desktop.compose_cycles_since_report = 0;
    desktop.compose_cycles_max = 0;
    context->api->write_serial("RAWLINE compositor text rendered\r\n");
    context->api->write_serial("RAWLINE compositor desktop composed\r\n");
    if (!desktop.api.create_thread(compositor_worker, nullptr)) return -1;
    return 0;
}
