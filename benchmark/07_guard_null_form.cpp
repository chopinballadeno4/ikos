// Test: Guard callee takes a pointer directly and checks internally
// Instead of passing bool, the caller passes the pointer itself.
// The callee compares the pointer to null and aborts.
// expected: *ptr = 42 → safe

#include <cstdlib>

extern int* get_ptr();

void guard_non_null(int* p) {
    if (p == nullptr) abort();
}

void test(void) {
    int* ptr = get_ptr();
    guard_non_null(ptr);
    *ptr = 42;
}

int main() { test(); return 0; }
