#include <stdint.h>

#include <rawline/taskbar_module.hpp>

namespace {

constexpr uint64_t clock_interval_ns = 1000000000;
constexpr uint32_t color_bar = 0x172536;
constexpr uint32_t color_top_edge = 0x526579;
constexpr uint32_t color_start = 0x2b4158;
constexpr uint32_t color_app = 0x26394d;
constexpr uint32_t color_text = 0xe4edf5;
constexpr uint32_t color_accent = 0xe5a235;
constexpr uint32_t color_menu = 0x223346;

struct State {
    const rawline::compositor_client::Api* api;
    rawline::wall_clock::Read read_wall_clock;
    rawline::compositor_client::SurfaceHandle bar;
    rawline::compositor_client::SurfaceHandle menu;
    uint64_t width;
    uint64_t height;
    uint64_t bar_height;
    uint64_t menu_width;
    uint64_t menu_height;
    uint64_t clock_x;
    uint8_t shown_hour;
    uint8_t shown_minute;
    bool have_time;
    bool menu_visible;
};

State state{};

bool paint_clock(const rawline::wall_clock::Time* time)
{
    char label[] = "--:--";
    if (time != nullptr && time->hour < 24 && time->minute < 60) {
        label[0] = static_cast<char>('0' + time->hour / 10);
        label[1] = static_cast<char>('0' + time->hour % 10);
        label[3] = static_cast<char>('0' + time->minute / 10);
        label[4] = static_cast<char>('0' + time->minute % 10);
        state.shown_hour = time->hour;
        state.shown_minute = time->minute;
        state.have_time = true;
    }
    const rawline::compositor_client::Rect area{
        static_cast<int64_t>(state.clock_x), 0, state.width - state.clock_x, state.bar_height};
    return state.api->fill_surface_rect(state.bar, area, color_bar) &&
        state.api->draw_surface_text(state.bar, static_cast<int64_t>(state.clock_x + 8), 14,
            label, color_text);
}

void clock_timer(uint64_t, void*)
{
    rawline::wall_clock::Time time{};
    if (!state.read_wall_clock(&time)) return;
    if (state.have_time && state.shown_hour == time.hour && state.shown_minute == time.minute) return;
    paint_clock(&time);
}

void taskbar_pointer(const rawline::compositor_client::PointerEvent* event, void*)
{
    if (event == nullptr || event->version != rawline::compositor_client::pointer_event_version ||
        event->size < sizeof(rawline::compositor_client::PointerEvent) ||
        (event->pressed & 1u) == 0 || event->surface_x < 0 || event->surface_x >= 112) return;
    state.menu_visible = !state.menu_visible;
    state.api->set_surface_visible(state.menu, state.menu_visible);
}

bool draw_initial_surfaces()
{
    const auto& api = *state.api;
    if (!api.fill_surface_rect(state.bar, {0, 0, state.width, state.bar_height}, color_bar) ||
        !api.fill_surface_rect(state.bar, {0, 0, 112, state.bar_height}, color_start) ||
        !api.fill_surface_rect(state.bar, {2, 2, state.width - 4, 1}, color_top_edge) ||
        !api.fill_surface_rect(state.bar, {120, 7, 116, 30}, color_app) ||
        !api.fill_surface_rect(state.bar, {12, 12, 18, 18}, color_accent) ||
        !api.draw_surface_text(state.bar, 38, 14, "RAWLINE", color_text) ||
        !api.draw_surface_text(state.bar, 132, 14, "Desktop", color_text)) return false;
    if (!api.fill_surface_rect(state.menu, {0, 0, state.menu_width, state.menu_height}, color_menu) ||
        !api.fill_surface_rect(state.menu, {0, 0, state.menu_width, 32}, color_start) ||
        !api.draw_surface_text(state.menu, 14, 10, "Rawline", color_text) ||
        !api.fill_surface_rect(state.menu, {12, 50, state.menu_width - 24, 32}, color_app) ||
        !api.draw_surface_text(state.menu, 24, 59, "Desktop", color_text) ||
        !api.draw_surface_text(state.menu, 14, 102, "No apps yet", color_text)) return false;
    rawline::wall_clock::Time time{};
    return paint_clock(state.read_wall_clock(&time) ? &time : nullptr);
}

}

extern "C" int64_t taskbar_entry(const rawline::taskbar_module::Context* context)
{
    if (context == nullptr || context->version != rawline::taskbar_module::context_version ||
        context->size < sizeof(rawline::taskbar_module::Context) || context->compositor == nullptr ||
        context->compositor->version != rawline::compositor_client::api_version ||
        context->compositor->size < sizeof(rawline::compositor_client::Api) ||
        context->read_wall_clock == nullptr || context->compositor->get_display_size == nullptr ||
        context->compositor->create_surface == nullptr || context->compositor->destroy_surface == nullptr ||
        context->compositor->set_surface_position == nullptr || context->compositor->set_surface_z_order == nullptr ||
        context->compositor->set_surface_visible == nullptr || context->compositor->fill_surface_rect == nullptr ||
        context->compositor->draw_surface_text == nullptr || context->compositor->set_pointer_handler == nullptr ||
        context->compositor->set_timer_handler == nullptr) return -1;

    state = {};
    state.api = context->compositor;
    state.read_wall_clock = context->read_wall_clock;
    if (!state.api->get_display_size(&state.width, &state.height) || state.width < 240 || state.height < 120)
        return -1;
    state.bar_height = state.height < 44 ? state.height : 44;
    state.clock_x = state.width > 96 ? state.width - 88 : 0;
    state.menu_width = state.width < 256 ? state.width : 256;
    state.menu_height = state.height - state.bar_height;
    if (state.menu_height > 196) state.menu_height = 196;
    if (!state.api->create_surface(state.width, state.bar_height,
            rawline::compositor_client::surface_flag_pointer_events, &state.bar) ||
        !state.api->create_surface(state.menu_width, state.menu_height, 0, &state.menu)) return -1;
    if (!state.api->set_surface_position(state.bar, 0, static_cast<int64_t>(state.height - state.bar_height)) ||
        !state.api->set_surface_z_order(state.bar, 1000) ||
        !state.api->set_surface_position(state.menu, 12,
            static_cast<int64_t>(state.height - state.bar_height - state.menu_height)) ||
        !state.api->set_surface_z_order(state.menu, 1100) ||
        !state.api->set_surface_visible(state.menu, false) ||
        !state.api->set_pointer_handler(state.bar, taskbar_pointer, nullptr) ||
        !draw_initial_surfaces() ||
        !state.api->set_timer_handler(state.bar, clock_interval_ns, clock_timer, nullptr) ||
        !state.api->set_surface_visible(state.bar, true)) return -1;
    return 0;
}
