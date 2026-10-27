// Test: No guard on array index — baseline true positive
// analysis: boa
// expected: arr[idx] → warning (idx may be out of bounds)

#include <cstdlib>

extern int get_idx();

int test(void) {
    int arr[10] = {0};
    int idx = get_idx();
    return arr[idx];  // unsafe: idx unconstrained
}

int main() { return test(); }
