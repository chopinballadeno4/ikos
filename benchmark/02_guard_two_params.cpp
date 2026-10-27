// Test: Guard with two bool parameters — guards both pointers in one call
// Callee aborts if either condition is false.
// expected: *p1, *p2 → both safe

#include <cstdlib>

extern int* get_ptr1();
extern int* get_ptr2();

void guard_both(bool a, bool b) {
    if (!a) abort();
    if (!b) abort();
}

void test(void) {
    int* p1 = get_ptr1();
    int* p2 = get_ptr2();
    guard_both(p1 != nullptr, p2 != nullptr);
    *p1 = 1;
    *p2 = 2;
}

int main() { test(); return 0; }
