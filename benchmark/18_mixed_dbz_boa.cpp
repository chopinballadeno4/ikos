// Test: Single guard ensures both divisor != 0 and index in bounds
// analysis: dbz, boa
// expected: arr[idx] and total/idx → both safe

#include <cstdlib>

extern int get_idx();

void guard_valid_index(int idx) {
    if (idx <= 0 || idx >= 10) abort();
}

int test(void) {
    int arr[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int idx = get_idx();
    guard_valid_index(idx);
    int total = 100;
    return arr[idx] + total / idx;  // safe: 0 < idx < 10
}

int main() { return test(); }
