#include <limits.h>
#include <stdint.h>

#include <rawline/kernel/display.hpp>

namespace {

const rawline::boot::BootInfo* active_boot_info;
rawline::pointer::Handler active_pointer_handler;
uint32_t channel_table[3][256];

bool valid(const rawline::boot::BootInfo* boot_info)
{
    if (boot_info == nullptr || boot_info->version != rawline::boot::boot_info_version) return false;
    const auto& framebuffer = boot_info->framebuffer;
    return framebuffer.address != 0 && framebuffer.width != 0 && framebuffer.height != 0 &&
        framebuffer.width <= UINT64_MAX / 4 && framebuffer.bits_per_pixel == 32 &&
        framebuffer.pitch >= framebuffer.width * 4 &&
        framebuffer.red_mask_size != 0 && framebuffer.green_mask_size != 0 &&
        framebuffer.blue_mask_size != 0 && framebuffer.red_mask_size <= 8 &&
        framebuffer.green_mask_size <= 8 && framebuffer.blue_mask_size <= 8 &&
        framebuffer.red_mask_shift + framebuffer.red_mask_size <= 32 &&
        framebuffer.green_mask_shift + framebuffer.green_mask_size <= 32 &&
        framebuffer.blue_mask_shift + framebuffer.blue_mask_size <= 32;
}

uint32_t channel(uint8_t value, uint8_t size, uint8_t shift)
{
    const uint64_t maximum = (uint64_t{1} << size) - 1;
    return static_cast<uint32_t>((uint64_t{value} * maximum / 255) << shift);
}

uint32_t pixel(const rawline::boot::FramebufferInfo& framebuffer, uint32_t rgb)
{
    return channel((rgb >> 16) & 0xff, framebuffer.red_mask_size, framebuffer.red_mask_shift) |
        channel((rgb >> 8) & 0xff, framebuffer.green_mask_size, framebuffer.green_mask_shift) |
        channel(rgb & 0xff, framebuffer.blue_mask_size, framebuffer.blue_mask_shift);
}

}

bool rawline::kernel::initialize_display(const boot::BootInfo* boot_info)
{
    if (!valid(boot_info)) return false;
    active_boot_info = boot_info;
    for (uint32_t value = 0; value < 256; ++value) {
        channel_table[0][value] = channel(static_cast<uint8_t>(value),
            boot_info->framebuffer.red_mask_size, boot_info->framebuffer.red_mask_shift);
        channel_table[1][value] = channel(static_cast<uint8_t>(value),
            boot_info->framebuffer.green_mask_size, boot_info->framebuffer.green_mask_shift);
        channel_table[2][value] = channel(static_cast<uint8_t>(value),
            boot_info->framebuffer.blue_mask_size, boot_info->framebuffer.blue_mask_shift);
    }
    return true;
}

bool rawline::kernel::get_software_display_info(graphics_backend::Info* info)
{
    const auto* boot_info = active_boot_info;
    if (!valid(boot_info) || info == nullptr) return false;
    info->version = graphics_backend::info_version;
    info->width = boot_info->framebuffer.width;
    info->height = boot_info->framebuffer.height;
    info->pitch_pixels = boot_info->framebuffer.pitch / 4;
    info->format = graphics_backend::format_rgbx8888;
    info->backend = graphics_backend::backend_software_framebuffer;
    info->capabilities = 0;
    return true;
}

bool rawline::kernel::present_display_rect(int64_t x, int64_t y, uint64_t width,
    uint64_t height, const uint32_t* pixels, uint64_t source_pitch_pixels)
{
    const auto* boot_info = active_boot_info;
    if (!valid(boot_info) || pixels == nullptr || width == 0 || height == 0 ||
        x < 0 || y < 0 || width > boot_info->framebuffer.width - static_cast<uint64_t>(x) ||
        height > boot_info->framebuffer.height - static_cast<uint64_t>(y) || source_pitch_pixels < width)
        return false;
    const auto& framebuffer = boot_info->framebuffer;
    auto* base = reinterpret_cast<volatile uint8_t*>(framebuffer.address);
    const bool standard_rgb = framebuffer.red_mask_size == 8 && framebuffer.green_mask_size == 8 &&
        framebuffer.blue_mask_size == 8 && framebuffer.red_mask_shift == 16 &&
        framebuffer.green_mask_shift == 8 && framebuffer.blue_mask_shift == 0;
    for (uint64_t row = 0; row < height; ++row) {
        auto* destination = reinterpret_cast<volatile uint32_t*>(base + (static_cast<uint64_t>(y) + row) * framebuffer.pitch) + x;
        const uint32_t* source = pixels + row * source_pitch_pixels;
        if (standard_rgb) {
            uint64_t count = width;
            asm volatile("cld; rep movsl"
                : "+D"(destination), "+S"(source), "+c"(count)
                : : "memory", "cc");
            continue;
        }
        for (uint64_t column = 0; column < width; ++column) {
            const uint32_t rgb = source[column];
            destination[column] = channel_table[0][(rgb >> 16) & 0xff] |
                channel_table[1][(rgb >> 8) & 0xff] | channel_table[2][rgb & 0xff];
        }
    }
    return true;
}

bool rawline::kernel::verify_software_display_rgb_at(uint64_t x, uint64_t y, uint32_t rgb)
{
    const auto* boot_info = active_boot_info;
    if (!valid(boot_info) || x >= boot_info->framebuffer.width ||
        y >= boot_info->framebuffer.height) return false;
    const auto& framebuffer = boot_info->framebuffer;
    const auto* base = reinterpret_cast<const volatile uint8_t*>(framebuffer.address);
    const auto* pixels = reinterpret_cast<const volatile uint32_t*>(base +
        y * framebuffer.pitch);
    return pixels[x] == pixel(framebuffer, rgb);
}

bool rawline::kernel::register_pointer_handler(pointer::Handler handler)
{
    if (handler == nullptr || active_pointer_handler != nullptr) return false;
    active_pointer_handler = handler;
    return true;
}

bool rawline::kernel::dispatch_pointer_event(const pointer::Event* event)
{
    if (event == nullptr || event->version != pointer::event_version ||
        event->size < sizeof(pointer::Event) || active_pointer_handler == nullptr) return false;
    active_pointer_handler(event);
    return true;
}
