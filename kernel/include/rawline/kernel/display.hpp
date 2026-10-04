#pragma once

#include <rawline/boot_info.hpp>
#include <rawline/display_module.hpp>
#include <rawline/block_module.hpp>
#include <rawline/graphics_backend.hpp>

namespace rawline::kernel {

bool initialize_display(const boot::BootInfo* boot_info);
bool get_software_display_info(graphics_backend::Info* info);
bool present_display_rect(int64_t x, int64_t y, uint64_t width, uint64_t height,
    const uint32_t* pixels, uint64_t source_pitch_pixels);
bool verify_software_display_rgb_at(uint64_t x, uint64_t y, uint32_t rgb);
bool register_pointer_handler(pointer::Handler handler);
bool dispatch_pointer_event(const pointer::Event* event);
bool initialize_block_device(const boot::BootInfo* boot_info, resource::OwnerId buffer_owner);
const block_module::Api* block_device_api();

}
