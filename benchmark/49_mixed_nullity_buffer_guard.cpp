// 49: nullity — opposite pointer facts on normal and exception flows.
// analysis: nullity
// expected: safe on BOTH dereferences; both flows are reachable.
// Normal: primary != null, backup == null -> dereference primary.
// Exception: primary == null, backup != null -> dereference backup.
// Program exit: both pointers are null or both are non-null.
// This example checks nullity only; external buffer sizes are unspecified.

#include <cstdlib>

extern char* get_buffer();
extern char* get_backup_buffer();

void check_buffer(char* primary, char* backup) {
  if (primary == nullptr) {
    if (backup == nullptr)
      std::abort();
    throw 1;
  }
  if (backup != nullptr)
    std::abort();
  // C_normal    = {primary != null && backup == null}
  // C_exception = {primary == null && backup != null}
}

void buffer_guard(char* primary, char* backup) {
  char* current = primary;
  char* recovery = backup;
  check_buffer(current, recovery);
  // C_normal    = {primary != null && backup == null} [Complete]
  // C_exception = {primary == null && backup != null} [Complete]
}

int test() {
  // Read before try: the catch handles only exceptions from buffer_guard.
  char* primary = get_buffer();
  char* backup = get_backup_buffer();
  try {
    buffer_guard(primary, backup);
    // Normal summary makes primary non-null.
    return *primary; // safe (nullity)
  } catch (...) {
    // Exception summary makes backup non-null, while primary is null.
    return *backup; // safe (nullity)
  }
}

int main() { return test(); }
