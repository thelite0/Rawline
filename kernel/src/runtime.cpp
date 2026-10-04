#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void* destination, const void* source, size_t count)
{
    auto* out = static_cast<volatile uint8_t*>(destination);
    const auto* in = static_cast<const volatile uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

extern "C" void* memset(void* destination, int value, size_t count)
{
    auto* out = static_cast<volatile uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t count)
{
    auto* out = static_cast<volatile uint8_t*>(destination);
    const auto* in = static_cast<const volatile uint8_t*>(source);
    if (out < in) {
        for (size_t i = 0; i < count; ++i) out[i] = in[i];
    } else if (out > in) {
        for (size_t i = count; i > 0; --i) out[i - 1] = in[i - 1];
    }
    return destination;
}
