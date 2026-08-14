#pragma once

#include "vthomas/abi.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace vthomas {

// The backend will resolve each pointer to allocation base, size, and interior
// byte offset before importing/binding it as Vulkan external memory.
struct buffer_argument {
  const void *pointer{};
  std::size_t minimum_bytes{};
};

struct resolved_buffer_argument {
  const void *allocation_base{};
  std::size_t allocation_bytes{};
  std::uint64_t byte_offset{};
};

class runtime_backend {
public:
  virtual ~runtime_backend() = default;

  virtual void launch(const kernel_descriptor &kernel,
                      std::size_t invocation_count,
                      std::span<const buffer_argument> buffers,
                      std::span<const std::uint32_t> push_constant_words) = 0;
};

// The caller retains ownership. Setting nullptr restores the unconfigured
// backend, whose launch method throws a descriptive std::logic_error.
void set_runtime_backend(runtime_backend *backend) noexcept;
runtime_backend &get_runtime_backend() noexcept;

inline void launch(const kernel_descriptor &kernel,
                   std::size_t invocation_count,
                   std::span<const buffer_argument> buffers,
                   std::span<const std::uint32_t> push_constant_words) {
  get_runtime_backend().launch(kernel, invocation_count, buffers,
                               push_constant_words);
}

// Return a complete push-constant payload after inserting the allocation
// offsets resolved by a HIP/Vulkan backend. The input payload already contains
// invocation_count and ordinary scalar arguments.
std::vector<std::uint32_t>
apply_buffer_offsets(const kernel_descriptor &kernel,
                     std::span<const std::uint32_t> push_constant_words,
                     std::span<const resolved_buffer_argument> buffers);

namespace detail {

void register_kernel(std::uintptr_t key, const kernel_descriptor *descriptor);
const kernel_descriptor &lookup_kernel(std::uintptr_t key);

template <class FunctionPointer>
std::uintptr_t function_key(FunctionPointer function) noexcept {
  static_assert(std::is_pointer_v<FunctionPointer> &&
                std::is_function_v<std::remove_pointer_t<FunctionPointer>>);
  static_assert(sizeof(FunctionPointer) == sizeof(std::uintptr_t),
                "VTHOMAS requires function pointers to fit in uintptr_t");
  return std::bit_cast<std::uintptr_t>(function);
}

template <class Parameter>
argument_descriptor make_argument_descriptor(std::uint32_t ordinal,
                                             std::uint32_t &word_offset,
                                             std::uint32_t &binding) {
  using P = std::remove_reference_t<Parameter>;
  argument_descriptor result{};
  result.ordinal = ordinal;
  result.word_offset = word_offset;

  if constexpr (std::is_pointer_v<P>) {
    using Element = std::remove_pointer_t<P>;
    using Value = std::remove_cv_t<Element>;
    static_assert(std::is_arithmetic_v<Value> ||
                      (std::is_class_v<Value> &&
                       std::is_standard_layout_v<Value> &&
                       std::is_trivially_copyable_v<Value>),
                  "VTHOMAS buffers require arithmetic or standard-layout, "
                  "trivially-copyable elements");
    result.kind = argument_kind::buffer;
    result.type = abi_type_for<Element>();
    result.word_count = 2;
    result.descriptor_set = 0;
    result.descriptor_binding = binding++;
    result.access = std::is_const_v<Element> ? buffer_access::read_only
                                             : buffer_access::read_write;
    word_offset += 2;
  } else {
    static_assert(packed_scalar<std::remove_cv_t<P>>,
                  "VTHOMAS scalars must be 8-, 16-, 32-, or 64-bit "
                  "arithmetic or enum values");
    result.kind = argument_kind::scalar;
    result.type = abi_type_for<P>();
    result.word_count = sizeof(P) <= sizeof(std::uint32_t) ? 1 : 2;
    result.access = buffer_access::read_only;
    word_offset += result.word_count;
  }
  return result;
}

template <class> class host_kernel_registration;

template <class R, class Index, class... Parameters>
class host_kernel_registration<R (*)(Index, Parameters...)> {
public:
  using function_type = R (*)(Index, Parameters...);

  host_kernel_registration(function_type function, std::string_view source,
                           std::string_view entry) {
    static_assert(std::is_void_v<R>, "VTHOMAS kernels must return void");
    static_assert(std::is_integral_v<Index> &&
                      (sizeof(Index) == 4 || sizeof(Index) == 8),
                  "the first VTHOMAS parameter must be a 32-bit or 64-bit "
                  "invocation index");

    std::uint32_t word_offset = 2; // uint64_t invocation count
    std::uint32_t binding = 0;
    std::uint32_t ordinal = 1;
    std::size_t output_index = 0;
    ((arguments_[output_index++] = make_argument_descriptor<Parameters>(
          ordinal++, word_offset, binding)),
     ...);

    descriptor_ = {
        abi_version,
        source,
        entry,
        default_workgroup_size,
        static_cast<std::uint32_t>(word_offset * sizeof(std::uint32_t)),
        arguments_};
    register_kernel(function_key(function), &descriptor_);
  }

private:
  std::array<argument_descriptor, sizeof...(Parameters)> arguments_{};
  kernel_descriptor descriptor_{};
};

template <class Parameter, class Argument>
void pack_launch_argument(const argument_descriptor &descriptor,
                          std::size_t invocation_count, Argument &&argument,
                          std::vector<buffer_argument> &buffers,
                          std::vector<std::uint32_t> &words) {
  using P = std::remove_reference_t<Parameter>;
  if constexpr (std::is_pointer_v<P>) {
    using Element = std::remove_pointer_t<P>;
    static_assert(std::is_convertible_v<Argument, P>,
                  "VTHOMAS pointer argument has the wrong type");
    P pointer = std::forward<Argument>(argument);
    buffers.push_back({static_cast<const void *>(pointer),
                       invocation_count * sizeof(Element)});
  } else {
    static_assert(std::is_convertible_v<Argument, P>,
                  "VTHOMAS scalar argument has the wrong type");
    store_scalar_words(words, descriptor.word_offset,
                       static_cast<P>(std::forward<Argument>(argument)));
  }
}

template <class... Parameters, class Tuple, std::size_t... I>
void pack_launch_arguments(std::span<const argument_descriptor> descriptors,
                           std::size_t invocation_count, Tuple &&arguments,
                           std::vector<buffer_argument> &buffers,
                           std::vector<std::uint32_t> &words,
                           std::index_sequence<I...>) {
  (pack_launch_argument<Parameters>(descriptors[I], invocation_count,
                                    std::get<I>(arguments), buffers, words),
   ...);
}

} // namespace detail

template <class R, class Index, class... Parameters, class... Arguments>
void parallel_for(std::size_t invocation_count,
                  R (*kernel)(Index, Parameters...), Arguments &&...arguments) {
  static_assert(sizeof...(Parameters) == sizeof...(Arguments),
                "VTHOMAS launch argument count does not match the kernel");
  const kernel_descriptor &descriptor =
      detail::lookup_kernel(detail::function_key(kernel));
  if (descriptor.arguments.size() != sizeof...(Parameters))
    throw std::logic_error("VTHOMAS host/compiler kernel ABI mismatch");

  std::vector<std::uint32_t> words(
      descriptor.push_constant_bytes / sizeof(std::uint32_t), 0);
  store_scalar_words(words, 0, static_cast<std::uint64_t>(invocation_count));

  std::vector<buffer_argument> buffers;
  buffers.reserve(
      (std::size_t{0} + ... +
       (std::is_pointer_v<std::remove_reference_t<Parameters>> ? 1 : 0)));
  auto arguments_tuple =
      std::forward_as_tuple(std::forward<Arguments>(arguments)...);
  detail::pack_launch_arguments<Parameters...>(
      descriptor.arguments, invocation_count, arguments_tuple, buffers, words,
      std::index_sequence_for<Parameters...>{});
  launch(descriptor, invocation_count, buffers, words);
}

} // namespace vthomas

#if !defined(VTHOMAS_DEVICE)
#ifndef VTHOMAS_DETAIL_JOIN_IMPL
#define VTHOMAS_DETAIL_JOIN_IMPL(a, b) a##b
#define VTHOMAS_DETAIL_JOIN(a, b) VTHOMAS_DETAIL_JOIN_IMPL(a, b)
#endif
#define VTHOMAS_REGISTER_KERNEL(kernel)                                        \
  namespace {                                                                  \
  const ::vthomas::detail::host_kernel_registration<decltype(&(kernel))>       \
      VTHOMAS_DETAIL_JOIN(__vthomas_host_registration_,                        \
                          kernel){&(kernel), #kernel, "vthomas_" #kernel};     \
  }                                                                            \
  static_assert(true)
#endif
