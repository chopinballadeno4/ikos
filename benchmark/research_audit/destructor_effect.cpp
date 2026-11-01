// Minimal real-error reproducer. Expected: division-by-zero diagnostic.
// The exception object is destroyed when the last handler exits.
struct ClearingError {
  int* target;
  explicit ClearingError(int* p) : target(p) {}
  ~ClearingError() { *target = 0; }
};

int main() {
  int divisor = 1;
  try {
    throw ClearingError(&divisor);
  } catch (...) {
  }
  return 100 / divisor; // actual divisor is zero
}
