// Test: int32_t args; NE constraint — guard ensures d != 0 via equality check
// analysis: dbz
// expected: WARNING — NE predicate cannot narrow interval domain

#include <cstdlib>
#include <cstdint>

extern int32_t get_a();
extern int32_t get_b();

void guard_nonzero(int32_t n) {
    if (n == 0) {
        abort();
    }
    // assume(n != 0)
    if (n > 0) {
        int32_t pos = n;
        (void)pos;
    } else {
        int32_t neg = -n;
        (void)neg;
    }

    // C_N = {n != 0}
}

int test(void) {
    int32_t a = get_a(), b = get_b();
    int32_t d = a - b;
    guard_nonzero(d);    // C_normal: p00 != 0 (NE) → interval stays [-inf,+inf]
    return 100 / d;      // warning: interval domain cannot represent "not zero"
}

int main() { return test(); }
