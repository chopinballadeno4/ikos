// Test: Basic guard_check — baseline case
// expected: *ptr = 42 → safe (our modification eliminates false positive)

#include <cstdlib>

extern int* get_ptr();

void guard_check(bool cond) {
    if (!cond) abort();
}

void test(void) {
    int* ptr = get_ptr();
    guard_check(ptr != nullptr);
    *ptr = 42;
}

int main() { test(); return 0; }
