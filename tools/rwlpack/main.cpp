#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include <rawline/rwl.hpp>

struct Elf64Header {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, program_offset, section_offset;
    uint32_t flags;
    uint16_t header_size, program_entry_size, program_count;
    uint16_t section_entry_size, section_count, section_name_index;
};

struct Elf64ProgramHeader {
    uint32_t type, flags;
    uint64_t offset, virtual_address, physical_address, file_size, memory_size, alignment;
};

static_assert(sizeof(Elf64Header) == 64);
static_assert(sizeof(Elf64ProgramHeader) == 56);

template <typename T>
const T* at(const std::vector<uint8_t>& bytes, uint64_t offset)
{
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return nullptr;
    return reinterpret_cast<const T*>(bytes.data() + offset);
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: rwlpack <kernel.elf> <kernel.rwl>\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> elf((std::istreambuf_iterator<char>(input)), {});
    auto* header = at<Elf64Header>(elf, 0);
    if (!header || std::memcmp(header->ident, "\x7f" "ELF", 4) != 0 ||
        header->ident[4] != 2 || header->ident[5] != 1 || header->machine != 62 ||
        header->program_entry_size != sizeof(Elf64ProgramHeader)) {
        std::cerr << "invalid x86-64 ELF input\n";
        return 1;
    }

    std::vector<rawline::rwl::Segment> segments;
    std::vector<uint64_t> source_offsets;
    uint64_t preferred_base = UINT64_MAX;
    for (uint16_t i = 0; i < header->program_count; ++i) {
        const uint64_t offset = header->program_offset + uint64_t{i} * header->program_entry_size;
        auto* ph = at<Elf64ProgramHeader>(elf, offset);
        if (!ph) { std::cerr << "truncated ELF program table\n"; return 1; }
        if (ph->type != 1 || ph->memory_size == 0) continue;
        if (ph->file_size > ph->memory_size || ph->offset > elf.size() || ph->file_size > elf.size() - ph->offset ||
            ph->virtual_address < 0xffffffffc0000000ull || ph->virtual_address >= 0xfffffffff0000000ull) {
            std::cerr << "unsupported or unsafe ELF load segment\n"; return 1;
        }
        uint32_t flags = rawline::rwl::segment_flag_read;
        if (ph->flags & 2) flags |= rawline::rwl::segment_flag_write;
        if (ph->flags & 1) flags |= rawline::rwl::segment_flag_execute;
        if (ph->virtual_address < preferred_base) preferred_base = ph->virtual_address;
        segments.push_back({ph->offset, ph->virtual_address,
            ph->file_size, ph->memory_size, flags, 0});
        source_offsets.push_back(ph->offset);
    }
    if (segments.empty() || segments.size() > rawline::rwl::maximum_segments) {
        std::cerr << "unsupported ELF segment count\n"; return 1;
    }
    if ((preferred_base & 0xfff) != 0 || header->entry < preferred_base) {
        std::cerr << "unsupported RWL link base or entry\n";
        return 1;
    }
    const uint64_t entry_offset = header->entry - preferred_base;
    bool entry_executable = false;
    for (const auto& s : segments) {
        if (header->entry >= s.image_offset && header->entry - s.image_offset < s.file_size &&
            (s.flags & rawline::rwl::segment_flag_execute)) entry_executable = true;
    }
    if (!entry_executable) { std::cerr << "entry is outside executable segment\n"; return 1; }
    for (const auto& segment : segments) {
        if (segment.image_offset < preferred_base || segment.image_offset - preferred_base >= 0x10000000ull) {
            std::cerr << "ELF segment is outside the supported RWL image window\n";
            return 1;
        }
    }
    const uint64_t table_end = sizeof(rawline::rwl::Header) + segments.size() * sizeof(rawline::rwl::Segment);
    uint64_t data_offset = (table_end + 0xfff) & ~uint64_t{0xfff};
    uint64_t image_size = data_offset;
    for (auto& s : segments) {
        s.image_offset -= preferred_base;
        s.file_offset = data_offset;
        data_offset = (data_offset + s.file_size + 0xfff) & ~uint64_t{0xfff};
        image_size = data_offset;
    }
    rawline::rwl::Header out_header{rawline::rwl::magic, static_cast<uint32_t>(table_end), rawline::rwl::version,
        rawline::rwl::architecture_x86_64, static_cast<uint16_t>(segments.size()), rawline::rwl::image_flag_relocatable,
        image_size, entry_offset, preferred_base, sizeof(rawline::rwl::Header)};
    std::vector<uint8_t> image(static_cast<size_t>(image_size), 0);
    std::memcpy(image.data(), &out_header, sizeof(out_header));
    std::memcpy(image.data() + sizeof(out_header), segments.data(), segments.size() * sizeof(segments[0]));
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& s = segments[i];
        std::memcpy(image.data() + s.file_offset, elf.data() + source_offsets[i], static_cast<size_t>(s.file_size));
    }

    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!output) { std::cerr << "failed writing RWL image\n"; return 1; }
    std::cout << "Packed " << segments.size() << " segments into " << image.size() << " bytes\n";
}
