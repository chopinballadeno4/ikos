// Test: An exception rejects out-of-bounds inputs before a normal return.
// analysis: boa
// expected: arr[idx] -> safe; normal condition is 0 <= idx && idx < 10.

extern int get_idx();

void require_index(int idx) {
    if (idx < 0 || idx >= 10) throw 1;

  // C_normal = {idx >= 0 && idx < 10}
  // C_exception = {idx < 0 || idx >= 10}
}

int test() {
    int arr[10] = {0};
    int idx = get_idx();
    try {
        require_index(idx);
        return arr[idx];
    } catch (...) {
        return 0;
    }
}

int main() { return test(); }
