// Test: Guard ensures loop count is positive, used as array bound
// analysis: boa
// expected: arr[i] inside loop → safe (0 <= i < n, n <= 100)

#include <cstdlib>

extern int get_n();

void guard_size(int n) {
    if (n <= 0 || n > 100) abort();
}

void test(void) {
    int arr[100] = {0};
    int n = get_n();
    guard_size(n);
    for (int i = 0; i < n; i++) {
        arr[i] = i;  // safe: 0 <= i < n <= 100
    }
}

int main() { test(); return 0; }
