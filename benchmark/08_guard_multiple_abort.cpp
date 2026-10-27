// Test: Guard callee has multiple abort points across different branches
// Aborts if ptr is null OR if size is non-positive.
// After the call both conditions hold: ptr != null AND size > 0.
// expected: *ptr = size → safe

#include <cstdlib>

extern int* get_ptr();
extern int  get_size();

void guard_valid(bool ptr_ok, bool size_ok) {
    if (!ptr_ok)  abort();
    if (!size_ok) abort();
}

void test(void) {
    int* ptr  = get_ptr();
    int  size = get_size();
    guard_valid(ptr != nullptr, size > 0);
    *ptr = size;
}

int main() { test(); return 0; }
