#include "vthomas/runtime.hpp"

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace vthomas {
namespace {

class unconfigured_backend final : public runtime_backend {
public:
  void launch(const kernel_descriptor &, std::size_t,
              std::span<const buffer_argument>,
              std::span<const std::uint32_t>) override {
    throw std::logic_error(
        "VTHOMAS runtime backend is not configured; Vulkan/HIP interop is not "
        "available until a runtime_backend is installed");
  }
};

unconfigured_backend fallback_backend;
std::atomic<runtime_backend *> selected_backend{&fallback_backend};

std::mutex &registry_mutex() {
  static std::mutex value;
  return value;
}

std::unordered_map<std::uintptr_t, const kernel_descriptor *> &
kernel_registry() {
  static std::unordered_map<std::uintptr_t, const kernel_descriptor *> value;
  return value;
}

} // namespace

void set_runtime_backend(runtime_backend *backend) noexcept {
  selected_backend.store(backend != nullptr ? backend : &fallback_backend,
                         std::memory_order_release);
}

runtime_backend &get_runtime_backend() noexcept {
  return *selected_backend.load(std::memory_order_acquire);
}

std::vector<std::uint32_t>
apply_buffer_offsets(const kernel_descriptor &kernel,
                     std::span<const std::uint32_t> push_constant_words,
                     std::span<const resolved_buffer_argument> buffers) {
  if (push_constant_words.size() * sizeof(std::uint32_t) !=
      kernel.push_constant_bytes)
    throw std::invalid_argument(
        "VTHOMAS push-constant payload size does not match reflection");

  std::size_t expected_buffers = 0;
  for (const argument_descriptor &argument : kernel.arguments)
    expected_buffers += argument.kind == argument_kind::buffer;
  if (buffers.size() != expected_buffers)
    throw std::invalid_argument(
        "VTHOMAS resolved buffer count does not match reflection");

  std::vector<std::uint32_t> result(push_constant_words.begin(),
                                    push_constant_words.end());
  std::size_t buffer_index = 0;
  for (const argument_descriptor &argument : kernel.arguments) {
    if (argument.kind != argument_kind::buffer)
      continue;
    const resolved_buffer_argument &buffer = buffers[buffer_index++];
    if (buffer.byte_offset > buffer.allocation_bytes)
      throw std::out_of_range(
          "VTHOMAS interior pointer lies outside its allocation");
    store_scalar_words(result, argument.word_offset, buffer.byte_offset);
  }
  return result;
}

namespace detail {

void register_kernel(std::uintptr_t key, const kernel_descriptor *descriptor) {
  std::lock_guard lock(registry_mutex());
  kernel_registry()[key] = descriptor;
}

const kernel_descriptor &lookup_kernel(std::uintptr_t key) {
  std::lock_guard lock(registry_mutex());
  const auto found = kernel_registry().find(key);
  if (found == kernel_registry().end())
    throw std::logic_error(
        "VTHOMAS kernel is not registered; add VTHOMAS_REGISTER_KERNEL");
  return *found->second;
}

} // namespace detail

} // namespace vthomas
