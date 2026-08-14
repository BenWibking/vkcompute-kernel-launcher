#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t SpirvMagic = 0x07230203;
constexpr std::uint16_t OpTypeVoid = 19;
constexpr std::uint16_t OpDecorate = 71;
constexpr std::uint32_t DecorationAliased = 20;
constexpr std::uint32_t DecorationBinding = 33;
constexpr std::uint32_t DecorationDescriptorSet = 34;

std::uint32_t readWord(const unsigned char *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::vector<std::uint32_t> readModule(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open input SPIR-V module: " + path);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
  if (bytes.size() % sizeof(std::uint32_t) != 0)
    throw std::runtime_error("SPIR-V byte size is not a multiple of four");
  std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
  for (std::size_t i = 0; i < words.size(); ++i)
    words[i] = readWord(bytes.data() + i * sizeof(std::uint32_t));
  if (words.size() < 5 || words[0] != SpirvMagic)
    throw std::runtime_error("input is not a little-endian SPIR-V module");
  return words;
}

void writeWord(std::ostream &output, std::uint32_t word) {
  const char bytes[] = {static_cast<char>(word), static_cast<char>(word >> 8),
                        static_cast<char>(word >> 16),
                        static_cast<char>(word >> 24)};
  output.write(bytes, sizeof(bytes));
}

void writeModule(const std::string &path,
                 std::span<const std::uint32_t> words) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot open output SPIR-V module: " + path);
  for (std::uint32_t word : words)
    writeWord(output, word);
  if (!output)
    throw std::runtime_error("failed writing output SPIR-V module: " + path);
}

struct DecorationSets {
  std::set<std::uint32_t> descriptor_sets;
  std::set<std::uint32_t> bindings;
  std::set<std::uint32_t> aliased;
  std::size_t first_type = 0;
};

DecorationSets inspect(std::span<const std::uint32_t> words) {
  DecorationSets result;
  for (std::size_t offset = 5; offset < words.size();) {
    const std::uint16_t word_count = words[offset] >> 16;
    const std::uint16_t opcode = words[offset] & 0xffff;
    if (word_count == 0 || offset + word_count > words.size())
      throw std::runtime_error("malformed SPIR-V instruction stream");

    if (opcode == OpDecorate && word_count >= 3) {
      const std::uint32_t target = words[offset + 1];
      switch (words[offset + 2]) {
      case DecorationAliased:
        result.aliased.insert(target);
        break;
      case DecorationBinding:
        result.bindings.insert(target);
        break;
      case DecorationDescriptorSet:
        result.descriptor_sets.insert(target);
        break;
      default:
        break;
      }
    }
    if (result.first_type == 0 && opcode == OpTypeVoid)
      result.first_type = offset;
    offset += word_count;
  }
  if (result.first_type == 0)
    throw std::runtime_error("SPIR-V module has no type section");
  return result;
}

std::vector<std::uint32_t>
addAliasedDecorations(std::span<const std::uint32_t> input) {
  const DecorationSets decorations = inspect(input);
  std::vector<std::uint32_t> targets;
  std::set_intersection(
      decorations.descriptor_sets.begin(), decorations.descriptor_sets.end(),
      decorations.bindings.begin(), decorations.bindings.end(),
      std::back_inserter(targets));
  std::erase_if(targets, [&](std::uint32_t target) {
    return decorations.aliased.contains(target);
  });

  std::vector<std::uint32_t> output;
  output.reserve(input.size() + 3 * targets.size());
  output.insert(output.end(), input.begin(),
                input.begin() + decorations.first_type);
  for (std::uint32_t target : targets) {
    output.push_back((3u << 16) | OpDecorate);
    output.push_back(target);
    output.push_back(DecorationAliased);
  }
  output.insert(output.end(), input.begin() + decorations.first_type,
                input.end());
  return output;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: vthomas_spirv_finalize INPUT.spv OUTPUT.spv\n";
    return 2;
  }
  try {
    const std::vector<std::uint32_t> input = readModule(argv[1]);
    const std::vector<std::uint32_t> output = addAliasedDecorations(input);
    writeModule(argv[2], output);
  } catch (const std::exception &error) {
    std::cerr << "vthomas_spirv_finalize: " << error.what() << '\n';
    return 1;
  }
}
