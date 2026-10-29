// Test: long long args; named sum expression passed as argument
// analysis: dbz
// expected: 1000/sum safe — sum in [1,500] after guard

#include <cstdlib>

extern long long get_a();
extern long long get_b();

void guard_range(long long n) {
    if (n < 1) abort();
    // assume(n >= 1)
    if (n > 1000) abort();
    // assume(n <= 1000)
    if (n % 2 == 0) return;
    if (n > 500) abort();  // odd > 500 rejected
    // assume(n <= 500)

    // C_N = {(n >= 1 && n % 2 != 0 && n <= 500), (n >= 1 && n <= 1000 && n % 2 == 0)}
}

int test(void) {
    long long a = get_a(), b = get_b();
    long long sum = a + b;
    guard_range(sum);
    return (int)(1000 / sum);
}

int main() { return test(); }
