// Test: Caller-visible memory is written before either kind of exit.
// analysis: dbz
// expected: both divisions -> safe (normal divisor = 10, caught divisor = 5).
// Tests preservation of existing IKOS memory effects on both continuations;
// safe results alone do not prove that an exception summary was applied.

extern bool get_flag();

void set_divisor(bool fail, int* divisor) {
    if (fail) {
        *divisor = 5;
        throw 1;
    }
    *divisor = 10;

  // Requires: divisor points to a live, writable int object.
  // C_normal = {fail == false}, *divisor = 10
  // C_exception = {fail == true}, *divisor = 5
}

int test() {
    bool fail = get_flag();
    int divisor = 0;
    try {
        set_divisor(fail, &divisor);
        return 100 / divisor;
    } catch (...) {
        return 100 / divisor;
    }
}

int main() { return test(); }
