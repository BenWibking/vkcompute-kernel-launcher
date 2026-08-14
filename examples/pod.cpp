#include "vthomas/vthomas.hpp"

#include <cstdint>

struct particle {
  double position;
  float weight;
  std::uint32_t species;
};

VTHOMAS_KERNEL void update_particles(vthomas::index_type i,
                                     vthomas::gptr<particle> particles,
                                     double displacement) {
  particles[i].position += displacement;
  particles[i].weight *= 2.0F;
}

VTHOMAS_REGISTER_KERNEL(update_particles);
