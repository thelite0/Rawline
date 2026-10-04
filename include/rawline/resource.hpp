#pragma once

#include <stdint.h>

namespace rawline::resource {

inline constexpr uint64_t identity_version = 1;

enum class OwnerKind : uint32_t {
    Kernel = 1,
    Process = 2,
    Thread = 3,
    Module = 4,
    Shared = 5,
    Domain = 6
};

struct OwnerId {
    uint64_t value;
    uint32_t generation;
    OwnerKind kind;
};

struct ResourceDomain {
    uint64_t value;
    uint32_t generation;
    OwnerKind kind;
};

struct AllocationHandle {
    uint32_t slot;
    uint32_t generation;
};

constexpr OwnerId owner_id(ResourceDomain domain)
{
    return {domain.value, domain.generation, domain.kind};
}

constexpr OwnerId owner_id(OwnerKind kind, uint64_t value, uint32_t generation = 1)
{
    return {value, generation, kind};
}

inline constexpr uint32_t allocation_flag_kernel_lifetime = 1;
inline constexpr uint32_t allocation_flag_shared = 2;

}
