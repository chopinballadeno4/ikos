// Test: Normal return, exception propagation and process termination coexist.
// analysis: dbz
// expected (program semantics): both divisions -> safe.
// n > 0 returns, n < 0 throws, n == 0 aborts and reaches neither division.
// Precision probe: an Unavailable exception summary may warn in catch.

#include <cstdlib>

extern int get_n();

void classify_exit(int n) {
    if (n > 0) return;
    if (n == 0) std::abort();
    throw 1;

  // C_normal = {n > 0}
  // C_exception = {n <= 0 && n != 0}
  // C_program = {n == 0}
}

int test() {
    int n = get_n();
    try {
        classify_exit(n);
        return 100 / n;
    } catch (...) {
        return 100 / n;
    }
}

int main() { return test(); }
