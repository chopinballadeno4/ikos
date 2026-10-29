// Test: Only the exceptional continuation implies a positive divisor.
// analysis: dbz
// expected (program semantics): 100 / n in catch -> safe; exception needs n > 0.
// Precision probe: C++ throw introduces runtime calls; an Unavailable exception
// summary may conservatively leave a warning. Normal return implies n <= 0.

extern int get_n();

void throw_if_positive(int n) {
    if (n > 1) throw 1;

  // C_normal = {n <= 0}
  // C_exception = {n > 0}
}

int test() {
    int n = get_n();
    try {
        throw_if_positive(n);
        return 0;
    } catch (...) {
        return 100 / (n - 2);
    }
}

int main() { return test(); }
