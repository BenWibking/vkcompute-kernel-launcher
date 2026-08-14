#include "vthomas/vthomas.hpp"

#include <cstdint>

VTHOMAS_KERNEL void scalar_mix(vthomas::index_type i,
                               vthomas::gptr<std::uint32_t> output,
                               std::int8_t x, std::uint16_t y, float z) {
  output[i] = static_cast<std::uint32_t>(x) + y + static_cast<std::uint32_t>(z);
}

VTHOMAS_REGISTER_KERNEL(scalar_mix);
