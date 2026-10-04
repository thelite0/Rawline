#pragma once

#include <stdint.h>

#include <rawline/block_module.hpp>
#include <rawline/boot_info.hpp>

namespace rawline::filesystem_module {

inline constexpr uint64_t api_version = 1;
inline constexpr uint64_t context_version = 1;
inline constexpr uint64_t maximum_name_bytes = 256;

struct FileInfo {
    uint64_t version;
    uint64_t size;
    uint8_t is_directory;
    uint8_t reserved[7];
    char name[maximum_name_bytes];
};

struct Api {
    uint64_t version;
    uint64_t size;
    bool (*mount)(uint64_t device);
    uint64_t (*list_directory)(const char* path, FileInfo* output, uint64_t capacity);
    int64_t (*open_read)(const char* path);
    int64_t (*read)(uint64_t handle, void* output, uint64_t bytes);
    int64_t (*create_file)(const char* path);
    bool (*write)(uint64_t handle, const void* data, uint64_t bytes);
    bool (*flush_close)(uint64_t handle);
};

struct Context {
    uint64_t version;
    uint64_t size;
    const block_module::Api* block_api;
    const Api** exported_api;
};

}
