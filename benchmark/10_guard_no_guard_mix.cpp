// Test: Same translation unit — one function guarded, one not
// Verifies our modification does not suppress true positives in nearby code.
// expected:
//   test_safe()   → *ptr = 42  safe    (guard eliminates false positive)
//   test_unsafe() → *ptr = 42  warning (no guard, true positive preserved)

#include <cstdlib>

extern int* get_ptr();

void guard_check(bool cond) {
    if (!cond) abort();
}

void test_safe(void) {
    int* ptr = get_ptr();
    guard_check(ptr != nullptr);
    *ptr = 42;
}

void test_unsafe(void) {
    int* ptr = get_ptr();
    *ptr = 42;
}

int main() {
    test_safe();
    test_unsafe();
    return 0;
}
