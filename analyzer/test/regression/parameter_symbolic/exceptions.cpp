#include <cstdlib>
#include <stdexcept>

struct UserError {
  int code;
  explicit UserError(int n) : code(n) {}
  ~UserError() {}
};
struct ConstructorThrows {
  int code;
  explicit ConstructorThrows(int n) {
    if (n > 700)
      throw 7;
    code = n;
  }
  ~ConstructorThrows() {}
};
struct ConstructorAborts {
  int code;
  explicit ConstructorAborts(int n) {
    if (n > 700)
      std::abort();
    code = n;
  }
};
struct UnknownConstructor {
  UnknownConstructor();
  ~UnknownConstructor();
};
struct UnknownCleanup {
  int code;
  explicit UnknownCleanup(int n) : code(n) {}
  ~UnknownCleanup();
};
struct AbortingCleanup {
  int code;
  explicit AbortingCleanup(int n) : code(n) {}
  ~AbortingCleanup() { std::abort(); }
};
struct AbortAfterMessage : std::runtime_error {
  AbortAfterMessage() : std::runtime_error("message") { std::abort(); }
};
struct OverlappingError {
  int code;
  OverlappingError() : code(256) {
    reinterpret_cast< unsigned char* >(&code)[1] = 0;
    if (code == 0)
      std::abort();
  }
};

extern "C" {
int fresh_exception_int();
int* fresh_exception_pointer();
void opaque_exception_call();

void positive31(int n) {
  if (n <= 0)
    throw 1;
}
void normal32(int n) {
  if (n < 0 || n >= 10)
    throw 1;
}
void exception33(int n) {
  if (n > 0)
    throw 1;
}
void exception34(int n) {
  if (n >= 0 && n < 10)
    throw 1;
}
void swallowed35(int n) {
  try {
    positive31(n);
  } catch (...) {
  }
}
void pointer36(int* p) {
  if (p == nullptr)
    throw 1;
}
void rethrow36(int* p) {
  try {
    pointer36(p);
  } catch (...) {
    throw;
  }
}
void memory37(bool fail, int* p) {
  if (fail) {
    *p = 5;
    throw 1;
  }
  *p = 10;
}
void memory_wrapper37(bool fail, int* p) {
  memory37(fail, p);
}
void local_memory37(bool fail) {
  int value;
  memory37(fail, &value);
}
void mismatched_memory37(bool fail, char* p) {
  memory37(fail, reinterpret_cast< int* >(p));
}
void volatile_memory37(bool fail, int* p) {
  *static_cast< volatile int* >(p) = 5;
  if (fail)
    throw 1;
}
void external_load37(int* p) {
  if (*p == 0)
    throw 1;
}
void class41(int n) {
  if (n <= 0)
    std::abort();
  if (n > 500)
    throw std::overflow_error("too large");
}
void user_class41(int n) {
  if (n > 500)
    throw UserError(n);
}
void constructor_throws41(int n) {
  if (n > 500)
    throw ConstructorThrows(n);
}
void constructor_aborts41(int n) {
  if (n > 500)
    throw ConstructorAborts(n);
}
void unknown_constructor41(int n) {
  if (n > 500)
    throw UnknownConstructor();
}
void unknown_cleanup41(int n) {
  if (n > 500)
    throw UnknownCleanup(n);
}
void catch_unknown_cleanup41(int n) {
  try {
    unknown_cleanup41(n);
  } catch (...) {
  }
}
void catch_aborting_cleanup41(int n) {
  try {
    if (n > 500)
      throw AbortingCleanup(n);
  } catch (...) {
  }
}
void catch_std_class41(int n) {
  try {
    class41(n);
  } catch (...) {
  }
}
void rethrow_std_class41(int n) {
  try {
    class41(n);
  } catch (...) {
    throw;
  }
}
void nondeterministic_constructor41(int n) {
  if (n > 500)
    throw AbortAfterMessage();
}
void unknown_message41(const char* message) {
  throw std::runtime_error(message);
}
void overlapping_class41(int n) {
  if (n > 500)
    throw OverlappingError();
}
void mixed39(int n) {
  if (n > 0)
    throw 1;
  opaque_exception_call();
}
void split40(int n) {
  if (n > 0)
    return;
  if (n == 0)
    std::abort();
  throw 1;
}
void ordinary_wrapper31(int n) {
  positive31(n);
}
void catch_and_rethrow(int n) {
  try {
    positive31(n);
  } catch (...) {
    if (n < -3)
      throw;
  }
}
void typed_catch_fallback(int n) {
  try {
    positive31(n);
  } catch (double) {
  }
}
int normal_division_safe() {
  int n = fresh_exception_int();
  try {
    positive31(n);
    return 100 / n;
  } catch (...) {
    return 0;
  }
}
int caught_division_safe() {
  int n = fresh_exception_int();
  try {
    exception33(n);
    return 0;
  } catch (...) {
    return 100 / n;
  }
}
int class_normal_division_safe() {
  int n = fresh_exception_int();
  try {
    class41(n);
    return 100 / n;
  } catch (...) {
    return 0;
  }
}
int class_caught_division_safe() {
  int n = fresh_exception_int();
  try {
    rethrow_std_class41(n);
    return 0;
  } catch (...) {
    return 100 / n;
  }
}
int class_zero_keeps_warning() {
  int n = fresh_exception_int();
  try {
    if (n <= 0)
      throw std::logic_error("nonpositive");
    return 0;
  } catch (...) {
    return 100 / n;
  }
}
int normal_bounds_safe() {
  int n = fresh_exception_int(), array[10] = {};
  try {
    normal32(n);
    return array[n];
  } catch (...) {
    return 0;
  }
}
int caught_bounds_safe() {
  int n = fresh_exception_int(), array[10] = {};
  try {
    exception34(n);
    return 0;
  } catch (...) {
    return array[n];
  }
}
int rethrow_pointer_safe() {
  int* p = fresh_exception_pointer();
  try {
    rethrow36(p);
    return *p;
  } catch (...) {
    return 0;
  }
}
int memory_effects_safe() {
  bool fail = fresh_exception_int() != 0;
  int value = 0;
  try {
    memory37(fail, &value);
    return 100 / value;
  } catch (...) {
    return 100 / value;
  }
}
int unknown_argument_keeps_warning() {
  int value = 0;
  try {
    memory37(fresh_exception_int() != 0, &value);
    return 100 / value;
  } catch (...) {
    // The argument-producing call can throw before memory37 writes value.
    return 100 / value;
  }
}
int unknown_memory_pointer_keeps_warning() {
  int* p = fresh_exception_pointer();
  memory37(false, p);
  return *p;
}
int null_memory_pointer_keeps_error() {
  memory37(false, nullptr);
  return 0;
}
int offset_memory_pointer_keeps_error() {
  int value = 0;
  memory37(false, &value + 1);
  return 0;
}
int mismatched_memory_pointer_keeps_error() {
  char value = 0;
  memory37(false, reinterpret_cast< int* >(&value));
  return 0;
}
int swallowed_keeps_warning() {
  int n = fresh_exception_int();
  swallowed35(n);
  return 100 / n;
}
int caught_zero_keeps_warning() {
  int n = fresh_exception_int();
  try {
    positive31(n);
    return 0;
  } catch (...) {
    return 100 / n;
  }
}
int unknown_exception_keeps_warning() {
  int n = fresh_exception_int();
  try {
    mixed39(n);
    return 0;
  } catch (...) {
    return 100 / n;
  }
}
}
