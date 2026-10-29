// Test: Only the exceptional continuation guarantees an in-bounds index.
// analysis: boa
// expected (program semantics): arr[idx] in catch -> safe; 0 <= idx && idx < 10.
// Precision probe: an Unavailable exception summary may leave a warning.

extern int get_idx();

void throw_if_in_bounds(int idx) {
    if (idx >= 0 && idx < 10) throw 1;

  // C_normal = {}
  // C_exception = {}
}

int test() {
    int arr[10] = {0};
    int idx = get_idx();
    try {
        throw_if_in_bounds(idx);
        return 0;
    } catch (...) {
        return arr[idx];
    }
}

int main() { return test(); }
