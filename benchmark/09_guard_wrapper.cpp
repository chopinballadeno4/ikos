// Test: Wrapper function that calls guard_check internally
// check_ptr(ptr) calls guard_check(ptr != nullptr) inside.
// The summary of check_ptr should reflect the null constraint transitively.
// expected: *ptr = 42 → safe (if interprocedural summary propagates transitively)

#include <cstdlib>

extern int* get_ptr();

void guard_check(bool cond) {
    if (!cond) abort();
}

void check_ptr(int* p) {
    guard_check(p != nullptr);
}

void test(void) {
    int* ptr = get_ptr();
    check_ptr(ptr);
    *ptr = 42;
}

int main() { test(); return 0; }
