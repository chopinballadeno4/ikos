// Test: long args; named offset (v = n-1) computed after guard
// analysis: dbz
// expected: 100/v safe — guard→n in [2,1000], then v=n-1 in [1,999]

#include <cstdlib>

extern long get_n();

void guard_ge2(long n) {
    if (n < 2) abort();
    // assume(n >= 2)
    if (n > 1000) abort();
    // assume(n <= 1000)
    long mid = n / 2;
    if (mid <= 0) abort();  // always false given n>=2, but adds a block
    // assume(n / 2 > 0)

    // C_N = {n >= 2 && n <= 1000 && n / 2 > 0}
}

int test(void) {
    long n = get_n();
    guard_ge2(n);          // narrows n to [2, 1000]
    long v = n - 1;        // v computed after guard: v in [1, 999]
    return (int)(100 / v);
}

int main() { return test(); }
