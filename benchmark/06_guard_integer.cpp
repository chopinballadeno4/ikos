// Test: Guard checking an integer range (not a pointer)
// After guard_positive(n), n > 0 should be known to the analyzer.
// Array access arr[n-1] is valid only if n >= 1.
// expected: arr[n-1] → safe (integer pruning from summary)

#include <cstdlib>

extern int get_n();

void guard_positive(int n) {
    if (n <= 0) abort();
}

void test(void) {
    int arr[100];
    int n = get_n();
    guard_positive(n);
    arr[n - 1] = 42;
}

int main() { test(); return 0; }
