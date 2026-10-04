#pragma once

#include <stdint.h>

#include <rawline/block_module.hpp>
#include <rawline/kernel_module.hpp>

namespace rawline::storage_module {

inline constexpr uint64_t api_version = 1;
inline constexpr uint64_t context_version = 1;

struct Context {
    uint64_t version;
    uint64_t size;
    const kernel_module::Api* kernel_api;
    const boot::BootInfo* boot_info;
    uint64_t module_id;
    uint32_t generation;
    const block_module::Api** exported_api;
};

}
