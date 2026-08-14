#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

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

#ifdef VTHOMAS_DEVICE
namespace detail {

// LLVM opaque pointers intentionally discard a buffer's element type.  A
// registration object emitted next to each kernel carries a removable copy of
// that information in its LLVM value type. The marker byte count is
//
//   1 + role + 4 * type_code
//
// where role is 0/1/2 for scalar/read-write/read-only and type_code follows
// vthomas::abi_type. This preserves integer signedness as well as the LLVM
// element type without depending on debug information or C++ mangled names.
template <class T> consteval std::size_t signature_type_code() {
  using U = std::remove_cv_t<T>;
  if constexpr (std::is_enum_v<U>)
    return signature_type_code<std::underlying_type_t<U>>();
  else if constexpr (std::is_same_v<U, float>)
    return 8;
  else if constexpr (std::is_same_v<U, double>)
    return 9;
  else if constexpr (std::is_integral_v<U> && sizeof(U) == 1)
    return std::is_signed_v<U> ? 0 : 1;
  else if constexpr (std::is_integral_v<U> && sizeof(U) == 2)
    return std::is_signed_v<U> ? 2 : 3;
  else if constexpr (std::is_integral_v<U> && sizeof(U) == 4)
    return std::is_signed_v<U> ? 4 : 5;
  else if constexpr (std::is_integral_v<U> && sizeof(U) == 8)
    return std::is_signed_v<U> ? 6 : 7;
  else {
    static_assert(std::is_class_v<U> && std::is_standard_layout_v<U> &&
                      std::is_trivially_copyable_v<U>,
                  "VTHOMAS buffer POD types must be standard-layout and "
                  "trivially copyable");
    return 10; // simple standard-layout POD buffer element
  }
}

template <std::size_t MarkerBytes, class T> struct signature_argument_tag {
  unsigned char marker[MarkerBytes]{};
  std::remove_cv_t<T> type{};
};

template <class T> struct signature_tag {
  using type = signature_argument_tag<1 + 4 * signature_type_code<T>(), T>;
};

template <class T>
struct signature_tag<T __attribute__((address_space(11))) *> {
  using type = signature_argument_tag<
      (std::is_const_v<T> ? 3 : 2) + 4 * signature_type_code<T>(), T>;
};

template <std::size_t I, class T>
struct signature_argument_slot : signature_tag<T>::type {};

template <class Sequence, class... Args> struct signature_arguments;

template <std::size_t... I, class... Args>
struct signature_arguments<std::index_sequence<I...>, Args...>
    : signature_argument_slot<I, Args>... {};

template <class> struct signature_record;

template <class R, class... Args> struct signature_record<R (*)(Args...)> {
  using function_type = R (*)(Args...);
  function_type function;
  signature_arguments<std::index_sequence_for<Args...>, Args...> arguments;
};

} // namespace detail
#endif

} // namespace vthomas

#if defined(__clang__)
#define VTHOMAS_KERNEL __attribute__((annotate("vthomas.kernel")))
#else
#define VTHOMAS_KERNEL
#endif

#ifdef VTHOMAS_DEVICE
#define VTHOMAS_DETAIL_JOIN_IMPL(a, b) a##b
#define VTHOMAS_DETAIL_JOIN(a, b) VTHOMAS_DETAIL_JOIN_IMPL(a, b)
#define VTHOMAS_REGISTER_KERNEL(kernel)                                        \
  extern "C" __attribute__((used))                                             \
  const ::vthomas::detail::signature_record<decltype(&(kernel))>               \
  VTHOMAS_DETAIL_JOIN(__vthomas_signature_, kernel) = {&(kernel), {}}
#endif
