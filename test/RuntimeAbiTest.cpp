#include "vthomas/vthomas.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

void saxpy(vthomas::index_type i, double *y, const double *x, double a) {
  y[i] += a * x[i];
}

VTHOMAS_REGISTER_KERNEL(saxpy);

struct particle {
  double position;
  float weight;
  std::uint32_t species;
};

void copy_particles(vthomas::index_type i, particle *output,
                    const particle *input) {
  output[i] = input[i];
}

VTHOMAS_REGISTER_KERNEL(copy_particles);

void scalar_mix(vthomas::index_type i, std::uint32_t *output, std::int8_t x,
                std::uint16_t y, float z) {
  output[i] = static_cast<std::uint32_t>(x) + y + static_cast<std::uint32_t>(z);
}

VTHOMAS_REGISTER_KERNEL(scalar_mix);

namespace {

class recording_backend final : public vthomas::runtime_backend {
public:
  void launch(const vthomas::kernel_descriptor &kernel,
              std::size_t invocation_count,
              std::span<const vthomas::buffer_argument> buffers,
              std::span<const std::uint32_t> push_constant_words) override {
    descriptor = &kernel;
    count = invocation_count;
    recorded_buffers.assign(buffers.begin(), buffers.end());
    recorded_words.assign(push_constant_words.begin(),
                          push_constant_words.end());
  }

  const vthomas::kernel_descriptor *descriptor = nullptr;
  std::size_t count = 0;
  std::vector<vthomas::buffer_argument> recorded_buffers;
  std::vector<std::uint32_t> recorded_words;
};

} // namespace

int main() {
  std::vector<std::uint32_t> words;
  vthomas::append_scalar_words(words, std::uint32_t{0x12345678});
  vthomas::append_scalar_words(words, std::uint64_t{0x0123456789abcdefULL});
  vthomas::append_scalar_words(words, 1.0F);
  vthomas::append_scalar_words(words, std::int8_t{-2});
  vthomas::append_scalar_words(words, std::uint16_t{0xfedc});

  assert((words == std::vector<std::uint32_t>{0x12345678, 0x89abcdef,
                                              0x01234567, 0x3f800000,
                                              0x000000fe, 0x0000fedc}));

  bool threw = false;
  try {
    vthomas::launch({}, 0, {}, {});
  } catch (const std::logic_error &) {
    threw = true;
  }
  assert(threw);

  double y[17]{};
  double x[17]{};
  recording_backend backend;
  vthomas::set_runtime_backend(&backend);
  vthomas::parallel_for(17, saxpy, y, x, 2.5);

  assert(backend.descriptor != nullptr);
  assert(backend.descriptor->source_name == "saxpy");
  assert(backend.descriptor->entry_point == "vthomas_saxpy");
  assert(backend.descriptor->workgroup_size == 256);
  assert(backend.descriptor->push_constant_bytes == 32);
  assert(backend.count == 17);
  assert(backend.recorded_buffers.size() == 2);
  assert(backend.recorded_buffers[0].pointer == y);
  assert(backend.recorded_buffers[1].pointer == x);
  assert(backend.recorded_buffers[0].minimum_bytes == sizeof(y));
  assert(backend.recorded_words.size() == 8);
  assert(backend.recorded_words[0] == 17);
  assert(backend.recorded_words[1] == 0);
  assert(backend.recorded_words[2] == 0);
  assert(backend.recorded_words[3] == 0);
  assert(backend.recorded_words[4] == 0);
  assert(backend.recorded_words[5] == 0);
  const std::uint64_t scalar_bits = std::bit_cast<std::uint64_t>(2.5);
  assert(backend.recorded_words[6] == static_cast<std::uint32_t>(scalar_bits));
  assert(backend.recorded_words[7] ==
         static_cast<std::uint32_t>(scalar_bits >> 32));

  const vthomas::resolved_buffer_argument resolved[] = {{y, sizeof(y), 16},
                                                        {x, sizeof(x), 24}};
  const auto resolved_words = vthomas::apply_buffer_offsets(
      *backend.descriptor, backend.recorded_words, resolved);
  assert(resolved_words[2] == 16);
  assert(resolved_words[3] == 0);
  assert(resolved_words[4] == 24);
  assert(resolved_words[5] == 0);

  vthomas::parallel_for(16, saxpy, y + 1, y, 2.5);
  assert(backend.recorded_buffers[0].pointer == y + 1);
  assert(backend.recorded_buffers[1].pointer == y);
  assert(backend.recorded_buffers[0].minimum_bytes == 16 * sizeof(double));
  const vthomas::resolved_buffer_argument overlapping[] = {
      {y, sizeof(y), sizeof(double)}, {y, sizeof(y), 0}};
  const auto overlapping_words = vthomas::apply_buffer_offsets(
      *backend.descriptor, backend.recorded_words, overlapping);
  assert(overlapping_words[2] == sizeof(double));
  assert(overlapping_words[4] == 0);

  particle output[3]{};
  particle input[3]{};
  vthomas::parallel_for(3, copy_particles, output, input);
  assert(backend.descriptor->source_name == "copy_particles");
  assert(backend.descriptor->arguments.size() == 2);
  assert(backend.descriptor->arguments[0].type == vthomas::abi_type::pod);
  assert(backend.descriptor->arguments[1].type == vthomas::abi_type::pod);
  assert(backend.recorded_buffers[0].minimum_bytes == sizeof(output));
  assert(backend.recorded_buffers[1].minimum_bytes == sizeof(input));

  std::uint32_t scalar_output[5]{};
  vthomas::parallel_for(5, scalar_mix, scalar_output, std::int8_t{-3},
                        std::uint16_t{513}, 1.5F);
  assert(backend.descriptor->push_constant_bytes == 28);
  assert(backend.recorded_words.size() == 7);
  assert(backend.recorded_words[4] == 0x000000fd);
  assert(backend.recorded_words[5] == 513);
  assert(backend.recorded_words[6] == std::bit_cast<std::uint32_t>(1.5F));

  vthomas::set_runtime_backend(nullptr);
}
