#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace vthomas {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr std::uint32_t default_workgroup_size = 256;

enum class argument_kind : std::uint8_t { buffer, scalar };
enum class abi_type : std::uint8_t {
  i8,
  u8,
  i16,
  u16,
  i32,
  u32,
  i64,
  u64,
  f32,
  f64,
  pod,
};
enum class buffer_access : std::uint8_t { read_only, read_write };

struct argument_descriptor {
  argument_kind kind{};
  abi_type type{};
  // Ordinal in the source signature. The implicit invocation index is zero
  // and is not itself represented by an argument_descriptor.
  std::uint32_t ordinal{};
  // For a scalar this is its payload location. For a buffer this optionally
  // locates the packed 64-bit interior byte offset (word_count == 2).
  std::uint32_t word_offset{};
  std::uint32_t word_count{};
  std::uint32_t descriptor_set{};
  std::uint32_t descriptor_binding{};
  buffer_access access{buffer_access::read_write};
};

struct kernel_descriptor {
  std::uint32_t format_version{abi_version};
  std::string_view source_name;
  std::string_view entry_point;
  std::uint32_t workgroup_size{default_workgroup_size};
  std::uint32_t push_constant_bytes{};
  std::span<const argument_descriptor> arguments;
};

template <class T>
concept packed_scalar =
    ((std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>) ||
     std::floating_point<T> || std::is_enum_v<T>) &&
    (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <packed_scalar T>
void append_scalar_words(std::vector<std::uint32_t> &words, T value) {
  if constexpr (std::is_enum_v<T>) {
    append_scalar_words(words, static_cast<std::underlying_type_t<T>>(value));
  } else if constexpr (std::integral<T> && sizeof(T) < 4) {
    using Unsigned = std::make_unsigned_t<T>;
    words.push_back(static_cast<std::uint32_t>(static_cast<Unsigned>(value)));
  } else if constexpr (sizeof(T) == 4) {
    words.push_back(std::bit_cast<std::uint32_t>(value));
  } else {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    words.push_back(static_cast<std::uint32_t>(bits));
    words.push_back(static_cast<std::uint32_t>(bits >> 32));
  }
}

template <packed_scalar T>
void store_scalar_words(std::span<std::uint32_t> words, std::size_t word_offset,
                        T value) {
  const std::size_t word_count = sizeof(T) <= sizeof(std::uint32_t) ? 1 : 2;
  if (word_offset > words.size() || word_count > words.size() - word_offset)
    throw std::out_of_range("VTHOMAS scalar does not fit the ABI payload");

  std::vector<std::uint32_t> packed;
  packed.reserve(word_count);
  append_scalar_words(packed, value);
  for (std::size_t i = 0; i < packed.size(); ++i)
    words[word_offset + i] = packed[i];
}

template <class T> consteval abi_type abi_type_for() {
  using U = std::remove_cv_t<T>;
  if constexpr (std::same_as<U, float>)
    return abi_type::f32;
  else if constexpr (std::same_as<U, double>)
    return abi_type::f64;
  else if constexpr (std::integral<U> && sizeof(U) == 1)
    return std::is_signed_v<U> ? abi_type::i8 : abi_type::u8;
  else if constexpr (std::integral<U> && sizeof(U) == 2)
    return std::is_signed_v<U> ? abi_type::i16 : abi_type::u16;
  else if constexpr (std::integral<U> && sizeof(U) == 4)
    return std::is_signed_v<U> ? abi_type::i32 : abi_type::u32;
  else if constexpr (std::integral<U> && sizeof(U) == 8)
    return std::is_signed_v<U> ? abi_type::i64 : abi_type::u64;
  else if constexpr (std::is_enum_v<U>)
    return abi_type_for<std::underlying_type_t<U>>();
  else {
    static_assert(std::is_class_v<U> && std::is_standard_layout_v<U> &&
                      std::is_trivially_copyable_v<U>,
                  "VTHOMAS buffer POD types must be standard-layout and "
                  "trivially copyable");
    return abi_type::pod;
  }
}

} // namespace vthomas
