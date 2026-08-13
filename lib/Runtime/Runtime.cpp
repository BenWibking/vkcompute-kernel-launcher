#include "vthomas/runtime.hpp"

#include <atomic>
#include <stdexcept>

namespace vthomas {
namespace {

class unconfigured_backend final : public runtime_backend {
public:
  void launch(const kernel_descriptor &, std::size_t,
              std::span<const buffer_argument>,
              std::span<const std::uint32_t>) override {
    throw std::logic_error(
        "VTHOMAS runtime backend is not configured; Vulkan/HIP interop is not "
        "implemented in the repository skeleton");
  }
};

unconfigured_backend fallback_backend;
std::atomic<runtime_backend *> selected_backend{&fallback_backend};

} // namespace

void set_runtime_backend(runtime_backend *backend) noexcept {
  selected_backend.store(backend != nullptr ? backend : &fallback_backend,
                         std::memory_order_release);
}

runtime_backend &get_runtime_backend() noexcept {
  return *selected_backend.load(std::memory_order_acquire);
}

} // namespace vthomas

