#pragma once

#include <cstddef>

// The device compilation uses the Vulkan storage-buffer address space. Host
// compilation intentionally sees an ordinary pointer so one source file can be
// compiled in the two-pass model described by the design.
namespace vthomas {

template <class T>
using gptr =
#ifdef VTHOMAS_DEVICE
    T __attribute__((address_space(11))) *;
#else
    T *;
#endif

using index_type = std::size_t;

} // namespace vthomas

#if defined(__clang__)
#define VTHOMAS_KERNEL __attribute__((annotate("vthomas.kernel")))
#else
#define VTHOMAS_KERNEL
#endif

