// Test: Guard ensures array index is in bounds
// analysis: boa
// expected: arr[idx] → safe

#include <cstdlib>

extern int get_idx();

void guard_in_bounds(int idx) {
    if (idx < 0 || idx >= 10) abort();
}

int test(void) {
    int arr[10] = {0};
    int idx = get_idx();
    guard_in_bounds(idx);
    return arr[idx];  // safe: 0 <= idx < 10
}

int main() { return test(); }
