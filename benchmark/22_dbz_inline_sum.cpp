// Test: unsigned args; expression written inline twice (CSE miss → two SSA vars)
// analysis: dbz
// expected: SAFE — rho reuses [1,10000] for the same unsigned a+b expression

#include <cstdlib>

extern unsigned get_a();
extern unsigned get_b();

void guard_pos(unsigned n) {
    unsigned check = n;
    if (check == 0) abort();
    // assume(check != 0)
    if (check > 10000) abort();
    // assume(check <= 10000)

    // C_N = {check != 0 && check <= 10000)
}

int test(void) {
    unsigned a = get_a(), b = get_b();
    guard_pos(a + b);        // arg: %tmp1 = a+b → narrowed to [1, 10000]
    return 100 / (a + b);   // fresh SSA result, same typed expression range
}

int main() { return test(); }
