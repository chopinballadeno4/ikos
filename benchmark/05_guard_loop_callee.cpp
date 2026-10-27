// Test: Guard callee contains a loop before the actual check
// The loop does not modify cond, so C_normal should still be p00 == 1.
// expected: *ptr = 42 → safe

#include <cstdlib>

extern int* get_ptr();
extern void do_work(int i);

void guard_check(bool cond) {
    for (int i = 0; i < 3; i++) {
        do_work(i);
    }
    if (!cond) abort();
}

void test(void) {
    int* ptr = get_ptr();
    guard_check(ptr != nullptr);
    *ptr = 42;
}

int main() { test(); return 0; }
