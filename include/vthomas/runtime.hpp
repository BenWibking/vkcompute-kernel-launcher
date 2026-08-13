#pragma once

#include "vthomas/abi.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace vthomas {

// The backend will resolve each pointer to allocation base, size, and interior
// byte offset before importing/binding it as Vulkan external memory.
struct buffer_argument {
  const void *pointer{};
  std::size_t minimum_bytes{};
};

class runtime_backend {
public:
  virtual ~runtime_backend() = default;

  virtual void launch(const kernel_descriptor &kernel,
                      std::size_t invocation_count,
                      std::span<const buffer_argument> buffers,
                      std::span<const std::uint32_t> scalar_words) = 0;
};

// The caller retains ownership. Setting nullptr restores the unconfigured
// backend, whose launch method throws a descriptive std::logic_error.
void set_runtime_backend(runtime_backend *backend) noexcept;
runtime_backend &get_runtime_backend() noexcept;

inline void launch(const kernel_descriptor &kernel,
                   std::size_t invocation_count,
                   std::span<const buffer_argument> buffers,
                   std::span<const std::uint32_t> scalar_words) {
  get_runtime_backend().launch(kernel, invocation_count, buffers, scalar_words);
}

} // namespace vthomas

