// Test: int64_t args; named scaled expression (d = 2*k) passed as argument
// analysis: dbz
// expected: 100/d safe — d same SSA var as guard arg, d in [1,2000]

#include <cstdlib>
#include <cstdint>

extern int64_t get_k();

void guard_even_pos(int64_t n) {
    if (n <= 0) {
        abort();
    } else if (n > 2000) {
        abort();
    } else {
        // assume(n > 0) && assume(n <= 2000)
        int64_t r = n % 2;
        if (r != 0) {
            if (n > 100) abort();  // odd > 100 rejected
        }
        // assume(r == 0) = assume(n % 2 == 0)
    }

    // C_N = {assume(n > 0) && assume(n <= 2000) && assume(n % 2 == 0)}
}

int test(void) {
    int64_t k = get_k();
    int64_t d = 2 * k;
    guard_even_pos(d);      // d is guard arg: apply_atom narrows d
    return (int)(100 / d);  // same SSA var d — safe
}

int main() { return test(); }
