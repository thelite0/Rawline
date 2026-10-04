#include <stdint.h>

#include <rawline/filesystem_module.hpp>

namespace {

constexpr uint32_t sector_bytes = 512;
constexpr uint32_t eoc = 0x0fffffffu;
constexpr uint32_t maximum_clusters_walk = 4096;

struct DirectoryEntry {
    char name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed));

struct OpenFile {
    bool active;
    bool writable;
    uint32_t first_cluster;
    uint32_t size;
    uint64_t directory_lba;
    uint16_t directory_offset;
    uint64_t position;
};

const rawline::block_module::Api* disk;
uint64_t device_id;
rawline::resource::AllocationHandle io_buffer_handle{};
uint8_t* io_buffer;
uint32_t sectors_per_cluster;
uint32_t reserved_sectors;
uint32_t fat_sectors;
uint32_t fat_count;
uint32_t root_cluster;
uint32_t cluster_count;
uint32_t first_data_sector;
uint32_t volume_sectors;
bool mounted;
OpenFile file;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (uint16_t{p[1]} << 8)); }
uint32_t u32(const uint8_t* p) { return uint32_t{p[0]} | (uint32_t{p[1]} << 8) |
    (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24); }
void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

bool read_sector(uint64_t lba)
{
    return mounted && lba < volume_sectors && disk->read_blocks(device_id, lba, 1, io_buffer_handle, 0);
}

bool write_sector(uint64_t lba)
{
    return mounted && lba < volume_sectors && disk->write_blocks(device_id, lba, 1, io_buffer_handle, 0);
}

uint32_t cluster_lba(uint32_t cluster)
{
    return first_data_sector + (cluster - 2) * sectors_per_cluster;
}

bool fat_get(uint32_t cluster, uint32_t* value)
{
    if (cluster >= cluster_count + 2 || value == nullptr) return false;
    const uint32_t byte_offset = cluster * 4;
    const uint32_t sector = reserved_sectors + byte_offset / sector_bytes;
    if (!read_sector(sector)) return false;
    *value = u32(io_buffer + byte_offset % sector_bytes) & 0x0fffffffu;
    return true;
}

bool fat_set(uint32_t cluster, uint32_t value)
{
    if (cluster >= cluster_count + 2) return false;
    const uint32_t byte_offset = cluster * 4;
    for (uint32_t copy = 0; copy < fat_count; ++copy) {
        const uint32_t sector = reserved_sectors + copy * fat_sectors + byte_offset / sector_bytes;
        if (!read_sector(sector)) return false;
        put32(io_buffer + byte_offset % sector_bytes, value & 0x0fffffffu);
        if (!write_sector(sector)) return false;
    }
    return true;
}

bool valid_cluster(uint32_t cluster)
{
    return cluster >= 2 && cluster < cluster_count + 2;
}

bool next_cluster(uint32_t cluster, uint32_t* next)
{
    return fat_get(cluster, next) && *next < 0x0ffffff8u && valid_cluster(*next);
}

bool name83(const char* path, char output[11])
{
    if (path == nullptr || *path++ != '/') return false;
    for (uint32_t i = 0; i < 11; ++i) output[i] = ' ';
    uint32_t base = 0, extension = 0;
    bool after_dot = false;
    for (uint32_t i = 0; i < 256 && path[i] != '\0'; ++i) {
        const char c = path[i];
        if (c == '/') return false;
        if (c == '.') { if (after_dot) return false; after_dot = true; continue; }
        if (c <= 0x20 || c >= 0x7f || c == '"' || c == '*' || c == '+' || c == ',' ||
            c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '[' ||
            c == '\\' || c == ']' || c == '|') return false;
        if (c >= 'a' && c <= 'z') {
            const char upper = static_cast<char>(c - 'a' + 'A');
            if (after_dot) { if (extension == 3) return false; output[8 + extension++] = upper; }
            else { if (base == 8) return false; output[base++] = upper; }
        } else if (after_dot) { if (extension == 3) return false; output[8 + extension++] = c; }
        else { if (base == 8) return false; output[base++] = c; }
    }
    return base != 0;
}

bool find_entry(const char name[11], uint64_t* lba, uint16_t* offset, DirectoryEntry* result,
    bool find_free)
{
    uint32_t cluster = root_cluster;
    for (uint32_t walked = 0; walked < maximum_clusters_walk; ++walked) {
        if (!valid_cluster(cluster)) return false;
        for (uint32_t sector_index = 0; sector_index < sectors_per_cluster; ++sector_index) {
            const uint64_t sector = cluster_lba(cluster) + sector_index;
            if (!read_sector(sector)) return false;
            for (uint16_t position = 0; position < sector_bytes; position += sizeof(DirectoryEntry)) {
                auto* entry = reinterpret_cast<DirectoryEntry*>(io_buffer + position);
                const uint8_t first = static_cast<uint8_t>(entry->name[0]);
                if (first == 0) {
                    if (find_free) { *lba = sector; *offset = position; return true; }
                    return false;
                }
                if (first == 0xe5 || entry->attributes == 0x0f) continue;
                bool match = true;
                for (uint32_t n = 0; n < 11; ++n) if (entry->name[n] != name[n]) match = false;
                if (match && !find_free) {
                    *lba = sector; *offset = position;
                    if (result != nullptr) *result = *entry;
                    return true;
                }
            }
        }
        uint32_t next;
        if (!next_cluster(cluster, &next)) break;
        cluster = next;
    }
    return false;
}

uint32_t allocate_cluster()
{
    for (uint32_t cluster = 2; cluster < cluster_count + 2; ++cluster) {
        uint32_t value;
        if (!fat_get(cluster, &value)) return 0;
        if (value == 0) {
            if (!fat_set(cluster, eoc)) return 0;
            for (uint32_t i = 0; i < sectors_per_cluster; ++i) {
                const uint64_t lba = cluster_lba(cluster) + i;
                if (!read_sector(lba)) return 0;
                for (uint32_t j = 0; j < sector_bytes; ++j) io_buffer[j] = 0;
                if (!write_sector(lba)) return 0;
            }
            return cluster;
        }
    }
    return 0;
}

uint64_t list_directory(const char* path, rawline::filesystem_module::FileInfo* output,
    uint64_t capacity)
{
    if (!mounted || path == nullptr || path[0] != '/' || path[1] != '\0') return 0;
    uint64_t found = 0;
    uint32_t cluster = root_cluster;
    for (uint32_t walked = 0; walked < maximum_clusters_walk; ++walked) {
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            if (!read_sector(cluster_lba(cluster) + s)) return found;
            for (uint16_t position = 0; position < sector_bytes; position += sizeof(DirectoryEntry)) {
                auto* entry = reinterpret_cast<DirectoryEntry*>(io_buffer + position);
                const uint8_t first = static_cast<uint8_t>(entry->name[0]);
                if (first == 0) return found;
                if (first == 0xe5 || entry->attributes == 0x0f) continue;
                if (output != nullptr && found < capacity) {
                    auto& item = output[found];
                    item = {};
                    item.version = 1;
                    item.size = entry->size;
                    item.is_directory = (entry->attributes & 0x10) != 0;
                    uint32_t out = 0;
                    for (uint32_t n = 0; n < 8 && entry->name[n] != ' '; ++n) item.name[out++] = entry->name[n];
                    if (entry->name[8] != ' ') {
                        item.name[out++] = '.';
                        for (uint32_t n = 8; n < 11 && entry->name[n] != ' '; ++n) item.name[out++] = entry->name[n];
                    }
                    item.name[out] = '\0';
                }
                ++found;
            }
        }
        uint32_t next;
        if (!next_cluster(cluster, &next)) break;
        cluster = next;
    }
    return found;
}

int64_t open_read(const char* path)
{
    char name[11];
    if (!mounted || !name83(path, name) || file.active) return -1;
    uint64_t lba;
    uint16_t offset;
    DirectoryEntry entry{};
    if (!find_entry(name, &lba, &offset, &entry, false) || (entry.attributes & 0x10)) return -1;
    file = {true, false, (uint32_t{entry.cluster_high} << 16) | entry.cluster_low,
        entry.size, lba, offset, 0};
    return 1;
}

int64_t read_file(uint64_t handle, void* output, uint64_t bytes)
{
    if (handle != 1 || !file.active || file.writable || output == nullptr) return -1;
    auto* destination = static_cast<uint8_t*>(output);
    const uint64_t remaining = file.size - file.position;
    const uint64_t amount = bytes < remaining ? bytes : remaining;
    if (amount == 0) return 0;
    uint32_t cluster = file.first_cluster;
    const uint64_t cluster_bytes = uint64_t{sectors_per_cluster} * sector_bytes;
    const uint64_t clusters_to_skip = file.position / cluster_bytes;
    for (uint64_t i = 0; i < clusters_to_skip; ++i) {
        uint32_t next;
        if (!next_cluster(cluster, &next)) return -1;
        cluster = next;
    }
    uint64_t copied = 0;
    uint64_t intra = file.position % cluster_bytes;
    while (copied < amount) {
        const uint64_t sector_index = intra / sector_bytes;
        const uint32_t in_sector = intra % sector_bytes;
        if (!read_sector(cluster_lba(cluster) + sector_index)) return -1;
        uint64_t chunk = sector_bytes - in_sector;
        if (chunk > amount - copied) chunk = amount - copied;
        for (uint64_t i = 0; i < chunk; ++i) destination[copied + i] = io_buffer[in_sector + i];
        copied += chunk; intra += chunk;
        if (intra == cluster_bytes && copied < amount) {
            uint32_t next;
            if (!next_cluster(cluster, &next)) return -1;
            cluster = next; intra = 0;
        }
    }
    file.position += copied;
    return copied;
}

int64_t create_file(const char* path)
{
    char name[11];
    if (!mounted || !name83(path, name) || file.active) return -1;
    uint64_t lba;
    uint16_t offset;
    DirectoryEntry existing{};
    if (find_entry(name, &lba, &offset, &existing, false)) return -1;
    if (!find_entry(name, &lba, &offset, nullptr, true) || !read_sector(lba)) return -1;
    auto* entry = reinterpret_cast<DirectoryEntry*>(io_buffer + offset);
    for (uint32_t i = 0; i < sizeof(DirectoryEntry); ++i) reinterpret_cast<uint8_t*>(entry)[i] = 0;
    for (uint32_t i = 0; i < 11; ++i) entry->name[i] = name[i];
    entry->attributes = 0x20;
    if (!write_sector(lba)) return -1;
    file = {true, true, 0, 0, lba, offset, 0};
    return 1;
}

bool write_file(uint64_t handle, const void* data, uint64_t bytes)
{
    if (handle != 1 || !file.active || !file.writable || data == nullptr ||
        bytes > UINT32_MAX - file.size) return false;
    const auto* source = static_cast<const uint8_t*>(data);
    const uint64_t cluster_bytes = uint64_t{sectors_per_cluster} * sector_bytes;
    uint64_t written = 0;
    while (written < bytes) {
        if (file.first_cluster == 0) {
            file.first_cluster = allocate_cluster();
            if (file.first_cluster == 0) return false;
        }
        uint32_t cluster = file.first_cluster;
        uint64_t cluster_index = file.size / cluster_bytes;
        for (uint64_t i = 0; i < cluster_index; ++i) {
            uint32_t next;
            if (!next_cluster(cluster, &next)) {
                next = allocate_cluster();
                if (next == 0 || !fat_set(cluster, next)) return false;
            }
            cluster = next;
        }
        const uint64_t in_cluster = file.size % cluster_bytes;
        const uint64_t sector_index = in_cluster / sector_bytes;
        const uint32_t in_sector = in_cluster % sector_bytes;
        const uint64_t lba = cluster_lba(cluster) + sector_index;
        if (!read_sector(lba)) return false;
        uint64_t chunk = sector_bytes - in_sector;
        if (chunk > bytes - written) chunk = bytes - written;
        for (uint64_t i = 0; i < chunk; ++i) io_buffer[in_sector + i] = source[written + i];
        if (!write_sector(lba)) return false;
        file.size += chunk; file.position = file.size; written += chunk;
    }
    return true;
}

bool flush_close(uint64_t handle)
{
    if (handle != 1 || !file.active) return false;
    if (file.writable) {
        if (!read_sector(file.directory_lba)) return false;
        auto* entry = reinterpret_cast<DirectoryEntry*>(io_buffer + file.directory_offset);
        entry->cluster_high = static_cast<uint16_t>(file.first_cluster >> 16);
        entry->cluster_low = static_cast<uint16_t>(file.first_cluster);
        entry->size = file.size;
        if (!write_sector(file.directory_lba)) return false;
    }
    if (!disk->flush(device_id)) return false;
    file = {};
    return true;
}

bool mount(uint64_t device);

rawline::filesystem_module::Api filesystem_api{
    rawline::filesystem_module::api_version, sizeof(rawline::filesystem_module::Api),
    mount, list_directory, open_read, read_file, create_file, write_file, flush_close
};

bool mount(uint64_t device)
{
    device_id = device;
    rawline::block_module::DeviceInfo info{};
    if (!disk->get_device_info(device, &info) || info.block_size != sector_bytes ||
        !disk->read_blocks(device, 0, 1, io_buffer_handle, 0)) return false;
    const uint8_t* b = io_buffer;
    if (b[510] != 0x55 || b[511] != 0xaa || u16(b + 11) != sector_bytes ||
        !b[13] || (b[13] & (b[13] - 1)) != 0 || u16(b + 14) == 0 || b[16] == 0) return false;
    sectors_per_cluster = b[13];
    reserved_sectors = u16(b + 14);
    fat_count = b[16];
    fat_sectors = u32(b + 36);
    root_cluster = u32(b + 44) & 0x0fffffff;
    volume_sectors = u32(b + 32);
    if (fat_sectors == 0 || volume_sectors == 0 || volume_sectors > info.block_count ||
        u16(b + 17) != 0 || u16(b + 22) != 0 || reserved_sectors + uint64_t{fat_count} * fat_sectors >= volume_sectors)
        return false;
    first_data_sector = reserved_sectors + fat_count * fat_sectors;
    cluster_count = (volume_sectors - first_data_sector) / sectors_per_cluster;
    if (cluster_count < 65525 || !valid_cluster(root_cluster)) return false;
    mounted = true;
    return true;
}

}

extern "C" int64_t filesystem_entry(const rawline::filesystem_module::Context* context)
{
    if (context == nullptr || context->version != rawline::filesystem_module::context_version ||
        context->size < sizeof(rawline::filesystem_module::Context) || context->block_api == nullptr ||
        context->block_api->version != rawline::block_module::api_version ||
        context->block_api->size < sizeof(rawline::block_module::Api) ||
        context->block_api->device_count == nullptr || context->block_api->get_device_info == nullptr ||
        context->block_api->read_blocks == nullptr || context->block_api->write_blocks == nullptr ||
        context->block_api->flush == nullptr || context->block_api->allocate_buffer == nullptr ||
        context->block_api->release_buffer == nullptr || context->block_api->write_serial == nullptr) return -1;
    if (context->exported_api == nullptr) return -1;
    disk = context->block_api;
    if (disk->device_count() == 0 || !disk->allocate_buffer(1, &io_buffer_handle,
        reinterpret_cast<void**>(&io_buffer))) return -1;
    mounted = false;
    *context->exported_api = &filesystem_api;
    disk->write_serial("RAWLINE filesystem.rwl: versioned FAT32 API ready\r\n");
    return 0;
}
