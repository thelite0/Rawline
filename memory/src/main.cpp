#include <stdint.h>

#include <rawline/memory_module.hpp>

namespace {

void write_text(const rawline::memory_module::Api* api, const char* text)
{
    api->write_serial(text);
}

void write_number(const rawline::memory_module::Api* api, uint64_t value)
{
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    char output[23];
    for (uint32_t i = 0; i < length; ++i) output[i] = digits[length - i - 1];
    output[length] = '\r';
    output[length + 1] = '\n';
    output[length + 2] = '\0';
    api->write_serial(output);
}

void write_inline_number(const rawline::memory_module::Api* api, uint64_t value)
{
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    char output[21];
    for (uint32_t i = 0; i < length; ++i) output[i] = digits[length - i - 1];
    output[length] = '\0';
    api->write_serial(output);
}

const char* owner_kind_name(rawline::resource::OwnerKind kind)
{
    using rawline::resource::OwnerKind;
    switch (kind) {
    case OwnerKind::Kernel: return "kernel";
    case OwnerKind::Process: return "process";
    case OwnerKind::Thread: return "thread";
    case OwnerKind::Module: return "module";
    case OwnerKind::Shared: return "shared";
    case OwnerKind::Domain: return "domain";
    }
    return "unknown";
}

}

extern "C" int64_t memory_entry(const rawline::memory_module::Context* context)
{
    if (context == nullptr || context->version != rawline::memory_module::context_version ||
        context->size < sizeof(rawline::memory_module::Context) || context->api == nullptr ||
        context->api->version != rawline::memory_module::api_version ||
        context->api->size < sizeof(rawline::memory_module::Api) ||
        context->api->get_stats == nullptr || context->api->get_owner_stats == nullptr ||
        context->api->get_owner_allocations == nullptr || context->api->create_domain == nullptr ||
        context->api->allocate_pages == nullptr || context->api->destroy_owner_domain == nullptr ||
        context->api->release_allocation == nullptr ||
        context->api->transfer_owner == nullptr || context->api->retain_shared == nullptr ||
        context->api->release_shared == nullptr || context->api->write_serial == nullptr) return -1;

#if defined(RAWLINE_TEST_MEMORY_FAULT)
    if (context->attempt == 1) asm volatile("ud2");
#endif

    rawline::memory_module::Stats stats{};
    if (!context->api->get_stats(context->boot_info, &stats) ||
        stats.version != rawline::memory_module::stats_version) return -1;

    write_text(context->api, "RAWLINE memory.rwl entered\r\n");
    write_text(context->api, "memory.rwl managed total pages: ");
    write_number(context->api, stats.total_pages);
    write_text(context->api, "memory.rwl managed free pages: ");
    write_number(context->api, stats.free_pages);
    write_text(context->api, "memory.rwl managed used pages: ");
    write_number(context->api, stats.used_pages);
    write_text(context->api, "memory.rwl owners: ");
    write_number(context->api, stats.owner_count);
    for (uint64_t i = 0; i < stats.owner_count; ++i) {
        rawline::memory_module::OwnerStats owner{};
        if (!context->api->get_owner_stats(context->boot_info, i, &owner) ||
            owner.version != rawline::memory_module::owner_stats_version) return -1;
        uint64_t allocation_pages = 0;
        for (uint64_t start = 0; start < owner.allocation_count; start += 16) {
            rawline::memory_module::AllocationInfo allocations[16];
            const uint64_t count = context->api->get_owner_allocations(
                context->boot_info, owner.owner, start, allocations, 16);
            if (count == 0) return -1;
            for (uint64_t allocation_index = 0; allocation_index < count; ++allocation_index) {
                const auto& allocation = allocations[allocation_index];
                if (allocation.version != rawline::memory_module::allocation_info_version ||
                    allocation.owner.value != owner.owner.value ||
                    allocation.owner.generation != owner.owner.generation ||
                    allocation.owner.kind != owner.owner.kind) return -1;
                allocation_pages += allocation.pages;
            }
        }
        if (allocation_pages != owner.used_pages || owner.total_pages != owner.used_pages ||
            owner.free_pages != 0) return -1;
        write_text(context->api, "  ");
        write_text(context->api, owner_kind_name(owner.owner.kind));
        write_text(context->api, " id=");
        write_inline_number(context->api, owner.owner.value);
        write_text(context->api, " gen=");
        write_inline_number(context->api, owner.owner.generation);
        write_text(context->api, " total/free/used=");
        write_inline_number(context->api, owner.total_pages);
        write_text(context->api, "/");
        write_inline_number(context->api, owner.free_pages);
        write_text(context->api, "/");
        write_inline_number(context->api, owner.used_pages);
        write_text(context->api, " allocations=");
        write_inline_number(context->api, owner.allocation_count);
        write_text(context->api, "\r\n");
    }
    return 0;
}
