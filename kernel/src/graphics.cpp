#include <stdint.h>

#include <rawline/kernel/display.hpp>
#include <rawline/kernel/graphics.hpp>

namespace {

bool gpu_selected;

bool get_info(rawline::graphics_backend::Info* info)
{
    return gpu_selected ? rawline::kernel::virtio_gpu_get_info(info)
        : rawline::kernel::get_software_display_info(info);
}

bool begin_frame() { return true; }

bool present_regions(const rawline::graphics_backend::Region* regions, uint64_t count,
    const uint32_t* pixels, uint64_t pitch)
{
    if (gpu_selected) return rawline::kernel::virtio_gpu_present(regions, count, pixels, pitch);
    rawline::graphics_backend::Info info{};
    if (regions == nullptr || count == 0 || pixels == nullptr || pitch == 0 ||
        !rawline::kernel::get_software_display_info(&info)) return false;
    for (uint64_t i = 0; i < count; ++i) {
        const auto& region = regions[i];
        if (region.x < 0 || region.y < 0 || region.width == 0 || region.height == 0 ||
            static_cast<uint64_t>(region.x) > info.width || static_cast<uint64_t>(region.y) > info.height ||
            region.width > info.width - static_cast<uint64_t>(region.x) ||
            region.height > info.height - static_cast<uint64_t>(region.y) || pitch < info.width) return false;
        if (!rawline::kernel::present_display_rect(region.x, region.y, region.width,
                region.height, pixels + static_cast<uint64_t>(region.y) * pitch + region.x, pitch))
            return false;
    }
    return true;
}

bool end_frame() { return true; }

}

bool rawline::kernel::initialize_graphics(const boot::BootInfo* boot_info)
{
    gpu_selected = initialize_virtio_gpu(boot_info);
    if (gpu_selected) return true;
    graphics_backend::Info fallback{};
    return get_software_display_info(&fallback);
}

const rawline::graphics_backend::Api* rawline::kernel::graphics_backend_api()
{
    static const graphics_backend::Api api{
        graphics_backend::api_version, sizeof(graphics_backend::Api),
        get_info, begin_frame, present_regions, end_frame
    };
    return &api;
}

bool rawline::kernel::verify_graphics_rgb_at(uint64_t x, uint64_t y, uint32_t rgb)
{
    return gpu_selected ? virtio_gpu_verify_rgb_at(x, y, rgb)
        : verify_software_display_rgb_at(x, y, rgb);
}
