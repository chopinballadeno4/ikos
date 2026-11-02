#include <duckdb/common/assert.hpp>
#include <ikos/analyzer/intrinsic.h>

extern "C" void probe_normal() {
  int n = __ikos_nondet_int();
  D_ASSERT(n > 0);
  __ikos_assert(n > 0);
}

extern "C" void probe_exception() {
  int n = __ikos_nondet_int();
  try {
    D_ASSERT(n > 0);
  } catch (...) {
    __ikos_assert(n <= 0);
  }
}
