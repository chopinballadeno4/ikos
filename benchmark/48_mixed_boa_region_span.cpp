// 48: boa — different index ranges on normal and exception flows.
// analysis: boa
// expected: safe on BOTH accesses; both flows are reachable.
// Normal: idx in [0,15] -> normal[idx].
// Exception: idx in [16,31] -> recovery[idx-16].
// Program exit: idx >= 32; it must not reach either access.

#include <cstddef>
#include <cstdlib>

extern size_t get_lo();

void check_region(size_t idx) {
  if (idx >= 32)
    std::abort();
  if (idx >= 16)
    throw 1;
  // C_normal    = {idx < 16}
  // C_exception = {16 <= idx && idx < 32}
}

void region_guard(size_t idx) {
  size_t shifted = idx + 2;
  size_t restored = shifted - 2; // modulo-size_t identity: restored == idx
  check_region(restored);
  // C_normal    = {idx < 16} [Complete]
  // C_exception = {16 <= idx && idx < 32} [Complete]
}

int test() {
  // Read before try: the catch handles only exceptions from region_guard.
  size_t idx = get_lo();
  char normal[16] = {};
  char recovery[16] = {};
  try {
    region_guard(idx);
    // Normal summary: idx in [0,15].
    return normal[idx]; // safe
  } catch (...) {
    // Exception summary: idx in [16,31], so idx-16 is in [0,15].
    return recovery[idx - 16]; // safe
    // 48_mixed_boa_region_span.cpp:41:12: warning: possible buffer overflow, could not bound index for access of local variable 'recovery' of 16 elements
    // 48_mixed_boa_region_span.cpp:41:12: warning: possible buffer overflow, accessing index between 1 and 16 of local variable 'recovery' of 16 elements
  }
}

int main() { return test(); }
