// Test: No guard for either division or array — both should warn
// analysis: dbz, boa
// expected: arr[idx] → warning, total/idx → warning (true positives)

#include <cstdlib>

extern int get_idx();

int test(void) {
    int arr[10] = {0};
    int idx = get_idx();
    int total = 100;
    return arr[idx] + total / idx;  // both unsafe
}

int main() { return test(); }
