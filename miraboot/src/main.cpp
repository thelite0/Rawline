#include <rawline/kernel_module.hpp>
#include <rawline/memory_module.hpp>
#include <rawline/display_module.hpp>
#include <rawline/input_module.hpp>
#include <rawline/storage_module.hpp>
#include <rawline/filesystem_module.hpp>
#include <rawline/compositor_client.hpp>
#include <rawline/wall_clock.hpp>
#include <rawline/taskbar_module.hpp>

namespace {

bool ascii_equal_ignore_case(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0') {
        char a = *left++, b = *right++;
        if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
        if (a != b) return false;
    }
    return *left == *right;
}

}

extern "C" int64_t miraboot_entry(const rawline::kernel_module::Context* context)
{
    if (context == nullptr || context->version != rawline::kernel_module::context_version ||
        context->size < sizeof(rawline::kernel_module::Context) || context->api == nullptr ||
        context->api->version != rawline::kernel_module::api_version ||
        context->api->size < sizeof(rawline::kernel_module::Api) ||
        context->api->write_serial == nullptr || context->api->find_module == nullptr ||
        context->api->start_module == nullptr || context->api->get_memory_stats == nullptr ||
        context->api->get_owner_stats == nullptr || context->api->get_owner_allocations == nullptr ||
        context->api->create_domain == nullptr || context->api->allocate_pages == nullptr ||
        context->api->release_allocation == nullptr ||
        context->api->destroy_owner_domain == nullptr || context->api->transfer_owner == nullptr ||
        context->api->retain_shared == nullptr || context->api->release_shared == nullptr ||
        context->display_api == nullptr ||
        context->display_api->version != rawline::display_module::api_version ||
        context->display_api->size < sizeof(rawline::display_module::Api) ||
        context->display_api->register_pointer_handler == nullptr || context->input_api == nullptr ||
        context->input_api->version != rawline::input_module::api_version ||
        context->input_api->size < sizeof(rawline::input_module::Api) ||
        context->api->read_wall_clock == nullptr) return -1;

    context->api->write_serial("RAWLINE Miraboot entered\r\n");
    const auto* module = context->api->find_module(context->boot_info, "memory.rwl");
    if (module == nullptr) return -1;
    const rawline::memory_module::Api memory_api{
        rawline::memory_module::api_version,
        sizeof(rawline::memory_module::Api),
        context->api->get_memory_stats,
        context->api->get_owner_stats,
        context->api->get_owner_allocations,
        context->api->create_domain,
        context->api->allocate_pages,
        context->api->release_allocation,
        context->api->destroy_owner_domain,
        context->api->transfer_owner,
        context->api->retain_shared,
        context->api->release_shared,
        context->api->write_serial
    };
    rawline::memory_module::Context memory_context{
        rawline::memory_module::context_version,
        sizeof(rawline::memory_module::Context),
        &memory_api,
        context->boot_info,
        1
    };
    if (context->api->start_module(context->boot_info, module, &memory_context) != 0) {
        context->api->write_serial("RAWLINE memory.rwl failed; restarting from kernel ownership state\r\n");
        memory_context.attempt = 2;
        if (context->api->start_module(context->boot_info, module, &memory_context) != 0) return -1;
    }
    context->api->write_serial("RAWLINE memory.rwl returned to Miraboot\r\n");

    const auto* storage = context->api->find_module(context->boot_info, "storage.rwl");
    const auto* filesystem = context->api->find_module(context->boot_info, "filesystem.rwl");
    if (storage == nullptr || filesystem == nullptr) return -1;
    const rawline::block_module::Api* block_api = nullptr;
    const rawline::storage_module::Context storage_context{
        rawline::storage_module::context_version,
        sizeof(rawline::storage_module::Context),
        context->api,
        context->boot_info,
        storage->id,
        storage->generation,
        &block_api
    };
    if (context->api->start_module(context->boot_info, storage, &storage_context) != 0 || block_api == nullptr)
        return -1;
    context->api->write_serial("RAWLINE storage.rwl returned a versioned block API\r\n");
    const rawline::filesystem_module::Api* filesystem_api = nullptr;
    const rawline::filesystem_module::Context filesystem_context{
        rawline::filesystem_module::context_version,
        sizeof(rawline::filesystem_module::Context),
        block_api,
        &filesystem_api
    };
    if (context->api->start_module(context->boot_info, filesystem, &filesystem_context) != 0 ||
        filesystem_api == nullptr || filesystem_api->version != rawline::filesystem_module::api_version ||
        filesystem_api->size < sizeof(rawline::filesystem_module::Api))
        return -1;
    if (!filesystem_api->mount(0)) return -1;
    context->api->write_serial("RAWLINE filesystem.rwl: FAT32 mounted\r\n");
    int64_t file_handle = filesystem_api->open_read("/hello.txt");
    if (file_handle >= 0) {
        char content[64]{};
        constexpr char expected[] = "hello rawline";
        const int64_t first_count = filesystem_api->read(static_cast<uint64_t>(file_handle), content, 5);
        const int64_t second_count = filesystem_api->read(static_cast<uint64_t>(file_handle), content + 5,
            sizeof(content) - 5);
        if (first_count != 5 || second_count != sizeof(expected) - 1 - 5 ||
            !filesystem_api->flush_close(static_cast<uint64_t>(file_handle))) return -1;
        for (uint64_t i = 0; i < sizeof(expected) - 1; ++i) if (content[i] != expected[i]) return -1;
        context->api->write_serial("RAWLINE persistence verified: hello.txt exact contents\r\n");
    } else {
        file_handle = filesystem_api->create_file("/hello.txt");
        constexpr char content[] = "hello rawline";
        if (file_handle < 0 || !filesystem_api->write(static_cast<uint64_t>(file_handle),
            content, sizeof(content) - 1) || !filesystem_api->flush_close(static_cast<uint64_t>(file_handle)))
            return -1;
        context->api->write_serial("RAWLINE persistence seed written: reboot to verify\r\n");
    }
    rawline::filesystem_module::FileInfo entries[8]{};
    const uint64_t entry_count = filesystem_api->list_directory("/", entries, 8);
    bool listed_hello = false;
    for (uint64_t i = 0; i < entry_count && i < 8; ++i)
        if (ascii_equal_ignore_case(entries[i].name, "HELLO.TXT")) listed_hello = true;
    if (!listed_hello) return -1;
    context->api->write_serial("RAWLINE filesystem list_directory verified\r\n");
    context->api->write_serial("RAWLINE filesystem.rwl persistence pass completed\r\n");

    const auto* compositor = context->api->find_module(context->boot_info, "compositor.rwl");
    if (compositor == nullptr) return -1;
    const rawline::compositor_client::Api* compositor_client_api = nullptr;
    const rawline::display_module::Context display_context{
        rawline::display_module::context_version,
        sizeof(rawline::display_module::Context),
        context->display_api,
        &compositor_client_api,
        context->graphics_api
    };
    if (context->api->start_module(context->boot_info, compositor, &display_context) != 0) return -1;
    context->api->write_serial("RAWLINE compositor returned to Miraboot\r\n");

    const auto* taskbar = context->api->find_module(context->boot_info, "taskbar.rwl");
    if (taskbar == nullptr || compositor_client_api == nullptr ||
        compositor_client_api->version != rawline::compositor_client::api_version ||
        compositor_client_api->size < sizeof(rawline::compositor_client::Api)) return -1;
    const rawline::taskbar_module::Context taskbar_context{
        rawline::taskbar_module::context_version,
        sizeof(rawline::taskbar_module::Context),
        compositor_client_api,
        context->api->read_wall_clock
    };
    if (context->api->start_module(context->boot_info, taskbar, &taskbar_context) != 0) return -1;
    context->api->write_serial("RAWLINE taskbar.rwl registered compositor surfaces\r\n");

    const auto* input = context->api->find_module(context->boot_info, "input.rwl");
    if (input == nullptr) return -1;
    const rawline::input_module::Context input_context{
        rawline::input_module::context_version,
        sizeof(rawline::input_module::Context),
        context->input_api
    };
    if (context->api->start_module(context->boot_info, input, &input_context) != 0) return -1;
    context->api->write_serial("RAWLINE input.rwl returned to Miraboot\r\n");
    return 0;
}
