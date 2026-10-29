// Test: size_t args; two named expressions (e0=x+1, e1=y+1) as guard args
// analysis: boa
// expected: arr[e0][e1] safe — e0 in [0,2], e1 in [0,3] after guard

#include <cstdlib>
#include <cstddef>

extern size_t get_x();
extern size_t get_y();

void guard_bounds(size_t r, size_t c) {
    if (r >= 3) abort();
    // assume(r < 3)
    if (c >= 4) abort();
    // assume(c < 4)
    if (r == 0) abort();  // reject origin (NE, dropped by interval)
    // assume(r != 0 || c != 0)

    // C_N = {(r < 3 && c < 4 && r != 0 ) || (r < 3 && c < 4 && c != 0 )}
}

int test(void) {
    size_t x = get_x(), y = get_y();
    size_t e0 = x + 1;
    size_t e1 = y + 1;
    int arr[4][5] = {};
    guard_bounds(e0, e1);  // e0 in [0,2], e1 in [0,3]
    // e0 = [1, 2], e1 = [1, 3]
    return arr[e0-1][e1-1];    // safe
}

int main() { return test(); }
