// Test: Catching an exception inside the guard removes its input guarantee.
// analysis: dbz
// expected: 100 / n -> warning; n == 0 is caught and then returns normally.
// C_normal must include both the direct return and the internally caught path.

extern int get_n();

void swallowed_guard(int n) {
    try {
        if (n <= 0) throw 1;
    } catch (...) {
        // Deliberately return to the caller even for invalid input.
    }

  // C_normal = {}
  // C_exception = {}
}

int test() {
    int n = get_n();
    swallowed_guard(n);
    return 100 / n;
}

int main() { return test(); }
