// Test: A wrapper catches and rethrows the pointer guard's exception.
// analysis: nullity
// expected: *ptr = 42 -> safe; normal return still implies ptr != nullptr.
// The rethrow path must not be joined into the wrapper's normal summary.

extern int* get_ptr();

void require_pointer(int* ptr) {
    if (ptr == nullptr) throw 1;

  // C_normal = {ptr != nullptr}
  // C_exception = {ptr == nullptr}
}

void rethrowing_wrapper(int* ptr) {
    try {
        require_pointer(ptr);
    } catch (...) {
        throw;
    }

    // C_normal = {}
    // C_exception = {}
}

void test() {
    int* ptr = get_ptr();
    try {
        rethrowing_wrapper(ptr);
        // ptr != nullptr
        *ptr = 42;
    } catch (...) {
        // ptr == nullptr
        // The pointer is not dereferenced on the exception continuation.
    }
}

int main() { test(); return 0; }
