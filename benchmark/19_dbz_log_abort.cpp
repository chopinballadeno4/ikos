// Test: Guard callee logs before aborting (extern side effect in abort path)
// analysis: dbz
// expected: 100 / n → safe

#include <cstdlib>

extern int get_n();
extern void log_error(const char* msg);

void guard_nonzero(int n) {
    if (n == 0) {
        log_error("divisor is zero");
        abort();
    }
}

int test(void) {
    int n = get_n();
    guard_nonzero(n);
    return 100 / n;  // safe
}

int main() { return test(); }
