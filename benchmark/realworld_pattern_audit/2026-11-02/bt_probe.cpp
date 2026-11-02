// Direct includes supply the header's ordinary transitive dependencies.
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <behaviortree_cpp/utils/convert_impl.hpp>
#include <ikos/analyzer/intrinsic.h>

extern "C" void probe_normal() {
  int n = __ikos_nondet_int();
  int8_t target;
  SafeAny::details::convertNumber(n, target);
  __ikos_assert(n >= -128 && n <= 127);
}

extern "C" void probe_exception() {
  int n = __ikos_nondet_int();
  try {
    SafeAny::details::checkUpperLimit<int, int8_t>(n);
  } catch (...) {
    __ikos_assert(n > 127);
  }
}
