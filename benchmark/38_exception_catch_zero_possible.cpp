// Test: A normal-return guarantee must never be applied to the catch flow.
// analysis: dbz
// expected: 100 / n in catch -> warning; n == 0 takes this catch path.
// The callee's normal condition n > 0 cannot make this division safe.

extern int get_n();

void require_positive(int n) {
    if (n > 0) throw 1;

  // C_normal = {n > 0}
  // C_exception = {n <= 0}
}

int test() {
    int n = get_n();
    try {
        require_positive(n);
        return 0;
    } catch (...) {
        return 100 / n;
    }

  // C_normal = {}
  // C_exception = {}
}

int main() { return test(); }
