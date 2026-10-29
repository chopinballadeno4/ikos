// Test: size_t args; named index (idx = base+offset) as guard arg
// analysis: boa
// expected: arr[idx] safe — idx in [2,7] after guard

#include <cstdlib>
#include <cstddef>

extern size_t get_base();
extern size_t get_offset();

void guard_index(size_t idx) {
    if (idx < 2) abort();
    // assume(idx >= 2)
    if (idx >= 10) abort();
    // assume(idx < 10)
    size_t aligned = (idx % 2 == 0) ? idx : idx - 1;
      // aligned = {idx % 2 == 0 - idx, idx % 2 != 0 - idx - 1}
    if (aligned < 2) abort();  // always false given idx>=2
    // assume(aligned >= 2)

  // C_N = {(idx >= 2 && idx < 8 && idx % 2 != 0 && idx - 1 >= 2) || (idx >= 2 && idx < 8 && idx % 2 == 0)}
}

int test(void) {
    size_t base = get_base(), offset = get_offset();
    size_t idx = base + offset;
    int arr[10] = {};
    guard_index(idx);  // idx in [2, 7]
    return arr[idx];   // safe
}

int main() { return test(); }
