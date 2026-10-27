// Test: No guard on divisor — baseline true positive
// analysis: dbz
// expected: 100 / n → warning (n may be zero)

#include <cstdlib>

extern int get_n();

int test(void) {
    int n = get_n();
    return 100 / n;  // unsafe: n may be zero
}

int main() { return test(); }
