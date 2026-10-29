// Test: A throwing guard constrains only its normal continuation.
// analysis: dbz
// expected: 100 / n -> safe; normal condition is n > 0.
// The catch path does not divide and must remain separate from normal return.

extern int get_n();

void require_positive(int n) {
    if (n <= 0) throw 1;

    // C_normal = {}
    // C_exception = {}
}

int test() {
    int n = get_n();
    try {
        require_positive(n);
        return 100 / n;
    } catch (...) {
        return 0;
    }
}

int main() { return test(); }
