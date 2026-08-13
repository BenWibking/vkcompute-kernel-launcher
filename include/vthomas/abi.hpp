#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace vthomas {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr std::uint32_t default_workgroup_size = 256;

enum class argument_kind : std::uint8_t { buffer, scalar };
enum class abi_type : std::uint8_t {
  i32,
  u32,
  i64,
  u64,
  f32,
  f64,
};
enum class buffer_access : std::uint8_t { read_only, read_write };

struct argument_descriptor {
  argument_kind kind{};
  abi_type type{};
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
    (std::integral<T> || std::floating_point<T> || std::is_enum_v<T>) &&
    (sizeof(T) == 4 || sizeof(T) == 8);

template <packed_scalar T>
void append_scalar_words(std::vector<std::uint32_t> &words, T value) {
  if constexpr (std::is_enum_v<T>) {
    append_scalar_words(words, static_cast<std::underlying_type_t<T>>(value));
  } else if constexpr (sizeof(T) == 4) {
    words.push_back(std::bit_cast<std::uint32_t>(value));
  } else {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    words.push_back(static_cast<std::uint32_t>(bits));
    words.push_back(static_cast<std::uint32_t>(bits >> 32));
  }
}

} // namespace vthomas
