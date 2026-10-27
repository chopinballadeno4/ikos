// Test: Guard ensures n > 0 (strictly positive) — stronger than nonzero
// Callee has nested if-else structure.
// analysis: dbz
// expected: 100 / n → safe

#include <cstdlib>

extern int get_n();

void guard_positive(int n) {
    if (n > 0) {
        // valid
    } else {
        abort();
    }
}

int test(void) {
    int n = get_n();
    guard_positive(n);
    return 100 / n;  // safe: n > 0 implies n != 0
}

int main() { return test(); }
