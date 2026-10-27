// Test: Guard ensures divisor is non-zero — division by zero check
// analysis: dbz
// expected: 100 / n → safe

#include <cstdlib>

extern int get_n();

void guard_nonzero(int n) {
    if (n == 0) abort();
}

int test(void) {
    int n = get_n();
    guard_nonzero(n);
    return 100 / n;  // safe: guard ensures n != 0
}

int main() { return test(); }
