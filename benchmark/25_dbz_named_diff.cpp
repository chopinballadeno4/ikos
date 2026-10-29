// Test: ptrdiff_t args; named difference (r = hi-lo) passed as argument
// analysis: dbz
// expected: 100/r safe — r > 0 after guard

#include <cstdlib>
#include <cstddef>

extern ptrdiff_t get_lo();
extern ptrdiff_t get_hi();

void guard_positive_range(ptrdiff_t n) {
    ptrdiff_t acc = 0;
    for (int i = 0; i < 3; i++) acc += 1;  // acc=3; loop adds back-edge to CFG
    if (acc != 3) abort();                  // always false; extra block
    // assume(acc == 3)
    if (n <= 0) abort();
    // assume(n > 0)
    if (n > 10000) abort();
    // assume(n <= 10000)

    // C_N = {n > 0 && n <= 10000}
}

int test(void) {
    ptrdiff_t lo = get_lo(), hi = get_hi();
    ptrdiff_t r = hi - lo;
    guard_positive_range(r);  // r is guard arg: r in [1, 10000]
    return (int)(100 / r);    // same SSA var r — safe
}

int main() { return test(); }
