#include <stdint.h>

#include <rawline/arch/x86_64/mouse.hpp>

namespace rawline::kernel::arch::x86_64 {
namespace {

constexpr uint16_t data_port = 0x60;
constexpr uint16_t status_port = 0x64;
constexpr uint16_t command_port = 0x64;
constexpr uint32_t wait_limit = 1000000;

uint8_t packet[3];
uint8_t packet_index;
bool mouse_ready;

uint8_t in8(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %w1, %0" : "=a"(value) : "d"(port));
    return value;
}

void out8(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %w1" : : "a"(value), "d"(port));
}

bool wait_input_empty()
{
    for (uint32_t attempt = 0; attempt < wait_limit; ++attempt)
        if ((in8(status_port) & 0x02) == 0) return true;
    return false;
}

bool write_command(uint8_t command)
{
    if (!wait_input_empty()) return false;
    out8(command_port, command);
    return true;
}

bool write_data(uint8_t data)
{
    if (!wait_input_empty()) return false;
    out8(data_port, data);
    return true;
}

bool read_data(uint8_t* data, bool auxiliary)
{
    if (data == nullptr) return false;
    for (uint32_t attempt = 0; attempt < wait_limit; ++attempt) {
        const uint8_t status = in8(status_port);
        if ((status & 0x01) == 0) continue;
        const uint8_t value = in8(data_port);
        if (((status & 0x20) != 0) != auxiliary) continue;
        *data = value;
        return true;
    }
    return false;
}

bool send_mouse_command(uint8_t command)
{
    uint8_t response;
    return write_command(0xd4) && write_data(command) && read_data(&response, true) &&
        response == 0xfa;
}

}

bool initialize_mouse()
{
    mouse_ready = false;
    packet_index = 0;
    if (!write_command(0xad) || !write_command(0xa7)) return false;
    while (in8(status_port) & 0x01) (void)in8(data_port);

    if (!write_command(0x20)) return false;
    uint8_t configuration;
    if (!read_data(&configuration, false)) return false;
    configuration = static_cast<uint8_t>((configuration & ~0x33u) | 0x04u);
    if (!write_command(0x60) || !write_data(configuration) || !write_command(0xa8) ||
        !send_mouse_command(0xf6) || !send_mouse_command(0xf4)) return false;

    mouse_ready = true;
    return true;
}

bool poll_mouse_event(pointer::Event* event)
{
    if (!mouse_ready || event == nullptr) return false;

    for (uint32_t consumed = 0; consumed < 16; ++consumed) {
        const uint8_t status = in8(status_port);
        if ((status & 0x01) == 0) return false;
        const uint8_t value = in8(data_port);
        if ((status & 0x20) == 0) continue;
        if (packet_index == 0 && (value & 0x08) == 0) continue;
        packet[packet_index++] = value;
        if (packet_index != 3) continue;

        packet_index = 0;
        const bool overflow = (packet[0] & 0xc0) != 0;
        const int32_t delta_x = overflow ? 0 : static_cast<int8_t>(packet[1]);
        const int32_t delta_y = overflow ? 0 : -static_cast<int32_t>(static_cast<int8_t>(packet[2]));
        event->version = pointer::event_version;
        event->size = sizeof(pointer::Event);
        event->delta_x = delta_x;
        event->delta_y = delta_y;
        event->buttons = packet[0] & 0x07;
        event->reserved = 0;
        return true;
    }
    return false;
}

}
