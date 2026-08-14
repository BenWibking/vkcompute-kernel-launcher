#include "vthomas/vthomas.hpp"

VTHOMAS_KERNEL void saxpy(vthomas::index_type i, vthomas::gptr<double> y,
                          vthomas::gptr<const double> x, double a) {
  y[i] += a * x[i];
}

VTHOMAS_REGISTER_KERNEL(saxpy);
