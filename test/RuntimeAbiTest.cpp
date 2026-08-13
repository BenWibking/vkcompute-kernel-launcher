#include "vthomas/vthomas.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

int main() {
  std::vector<std::uint32_t> words;
  vthomas::append_scalar_words(words, std::uint32_t{0x12345678});
  vthomas::append_scalar_words(words, std::uint64_t{0x0123456789abcdefULL});
  vthomas::append_scalar_words(words, 1.0F);

  assert((words == std::vector<std::uint32_t>{
                       0x12345678, 0x89abcdef, 0x01234567, 0x3f800000}));

  bool threw = false;
  try {
    vthomas::launch({}, 0, {}, {});
  } catch (const std::logic_error &) {
    threw = true;
  }
  assert(threw);
}

