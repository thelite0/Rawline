#pragma once

#include <rawline/boot_info.hpp>
#include <rawline/graphics_backend.hpp>

namespace rawline::kernel {

bool initialize_graphics(const boot::BootInfo* boot_info);
const graphics_backend::Api* graphics_backend_api();
bool verify_graphics_rgb_at(uint64_t x, uint64_t y, uint32_t rgb);
bool initialize_virtio_gpu(const boot::BootInfo* boot_info);
bool virtio_gpu_ready();
bool virtio_gpu_present(const graphics_backend::Region* regions, uint64_t region_count,
    const uint32_t* pixels, uint64_t source_pitch_pixels);
bool virtio_gpu_get_info(graphics_backend::Info* info);
bool virtio_gpu_verify_rgb_at(uint64_t x, uint64_t y, uint32_t rgb);

}
