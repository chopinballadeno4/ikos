// Diagnostic caller of the unmodified real header, not a project benchmark.
#include <fmt/core.h>
#include <ikos/analyzer/intrinsic.h>

extern "C" void probe_normal() {
  int n = __ikos_nondet_int();
  (void)fmt::detail::to_unsigned(n);
  __ikos_assert(n >= 0);
}
