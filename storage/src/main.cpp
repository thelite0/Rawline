#include <rawline/storage_module.hpp>

namespace {

const rawline::kernel_module::Api* kernel_api;
rawline::resource::ResourceDomain buffer_domain;

uint64_t device_count()
{
    rawline::block_module::DeviceInfo info{};
    return kernel_api->block_device_info(0, &info) ? 1 : 0;
}

bool get_info(uint64_t device, rawline::block_module::DeviceInfo* info)
{
    return kernel_api->block_device_info(device, info);
}

bool read(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle handle, uint64_t offset)
{
    return kernel_api->block_read(device, lba, count, handle, offset);
}

bool write(uint64_t device, uint64_t lba, uint32_t count,
    rawline::resource::AllocationHandle handle, uint64_t offset)
{
    return kernel_api->block_write(device, lba, count, handle, offset);
}

bool flush(uint64_t device)
{
    return kernel_api->block_flush(device);
}

bool allocate(uint64_t pages, rawline::resource::AllocationHandle* handle, void** address)
{
    return kernel_api->allocate_dma_buffer(rawline::resource::owner_id(buffer_domain),
        pages, handle, address);
}

bool release(rawline::resource::AllocationHandle handle)
{
    return kernel_api->release_dma_buffer(handle);
}

void serial(const char* text)
{
    if (text != nullptr) kernel_api->write_serial(text);
}

const rawline::block_module::Api api{
    rawline::block_module::api_version, sizeof(rawline::block_module::Api),
    device_count, get_info, read, write, flush, allocate, release, serial
};

}

extern "C" int64_t storage_entry(const rawline::storage_module::Context* context)
{
    if (context == nullptr || context->version != rawline::storage_module::context_version ||
        context->size < sizeof(rawline::storage_module::Context) || context->kernel_api == nullptr ||
        context->kernel_api->version != rawline::kernel_module::api_version ||
        context->kernel_api->size < sizeof(rawline::kernel_module::Api) || context->boot_info == nullptr ||
        context->module_id == 0 || context->generation == 0 || context->exported_api == nullptr ||
        context->kernel_api->initialize_block_device == nullptr ||
        context->kernel_api->block_device_info == nullptr || context->kernel_api->block_read == nullptr ||
        context->kernel_api->block_write == nullptr || context->kernel_api->block_flush == nullptr ||
        context->kernel_api->allocate_dma_buffer == nullptr || context->kernel_api->release_dma_buffer == nullptr)
        return -1;
    kernel_api = context->kernel_api;
    kernel_api->write_serial("RAWLINE storage.rwl entered\r\n");
    buffer_domain = kernel_api->create_domain(rawline::resource::OwnerKind::Shared);
    if (buffer_domain.value == 0) { serial("RAWLINE storage: shared buffer domain failed\r\n"); return -1; }
    if (!kernel_api->initialize_block_device(context->boot_info,
        rawline::resource::owner_id(buffer_domain))) {
        serial("RAWLINE storage: VirtIO block initialization failed\r\n"); return -1;
    }
    rawline::block_module::DeviceInfo info{};
    if (!get_info(0, &info) || info.block_size != rawline::block_module::sector_size) return -1;
    *context->exported_api = &api;
    serial("RAWLINE storage.rwl: VirtIO block device ready\r\n");
    return 0;
}
