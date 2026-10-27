// Test: Guard ensures two divisors are both non-zero
// analysis: dbz
// expected: a/b and c/d → both safe

#include <cstdlib>

extern int get_a();
extern int get_b();

void guard_nonzero2(int x, int y) {
    if (x == 0) abort();
    if (y == 0) abort();
}

int test(void) {
    int a = get_a();
    int b = get_b();
    guard_nonzero2(a, b);
    return (a / b) + (b / a);  // safe: both non-zero
}

int main() { return test(); }
