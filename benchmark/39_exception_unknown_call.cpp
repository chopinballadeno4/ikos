// Test: An unknown call supplies an additional possible exception origin.
// analysis: dbz
// expected: 100 / n in catch -> warning; may_throw() may throw even for n == 0.
// It would be unsound to use only the explicit throw condition n > 0.
// Unsupported exception origins must cause conservative fallback, not pruning.

extern int get_n();
extern void may_throw();

void throw_or_call(int n) {
    if (n > 0) throw 1;
    may_throw();

  // C_normal = {n > 0}
  // C_exception = {n <= 0}
}

int test() {
    int n = get_n();
    try {
        throw_or_call(n);
        return 0;
    } catch (...) {
        return 100 / n;
    }
}

int main() { return test(); }
