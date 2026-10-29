// Test: unsigned args; guard on n-1 refines the original divisor n
// analysis: dbz
// expected: SAFE — unsigned(n-1) in [1,1000] implies n in [2,1001]

#include <cstdlib>

extern unsigned get_n();

void guard_offset_pos(unsigned n) {
    unsigned threshold = 1;
    if (n < threshold) abort();
    // assume(n >= threshold)
    if (n > 1000) abort();
    // assume(n <= 1000)

    // C_N = {n >= 1 && n <= 1000}
}

int test(void) {
    unsigned n = get_n();
    guard_offset_pos(n - 1);  // inverse projection also refines n
    return 100 / n;           // safe: n is in [2,1001]
}

int main() { return test(); }
