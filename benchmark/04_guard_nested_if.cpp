// Test: Guard callee has nested if-else structure
// The callee checks cond via nested branches instead of a single if(!cond).
// expected: *ptr = 42 → safe

#include <cstdlib>

extern int* get_ptr();

void guard_check(bool cond) {
    if (cond) {
        // valid — do nothing
    } else {
        abort();
    }
}

void test(void) {
    int* ptr = get_ptr();
    guard_check(ptr != nullptr);
    *ptr = 42;
}

int main() { test(); return 0; }
