// Test: Guard callee calls an extern function before abort
// The extern log_error() call introduces side effects inside the callee,
// but the normal exit condition should still be p00 == 1.
// expected: *ptr = 42 → safe

#include <cstdlib>

extern int* get_ptr();
extern void log_error(const char* msg);

void guard_check(bool cond) {
    if (!cond) {
        log_error("guard failed");
        abort();
    }
}

void test(void) {
    int* ptr = get_ptr();
    guard_check(ptr != nullptr);
    *ptr = 42;
}

int main() { test(); return 0; }
