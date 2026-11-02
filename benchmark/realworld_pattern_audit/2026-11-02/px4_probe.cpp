#include <cassert>
#include <matrix/Matrix.hpp>
#include <ikos/analyzer/intrinsic.h>

extern "C" void probe_normal() {
  unsigned i = __ikos_nondet_uint();
  matrix::Matrix<int, 4, 1> m;
  m(i, 0) = 1;
  __ikos_assert(i < 4);
  int a[4] = {};
  a[i] = 1;
}
