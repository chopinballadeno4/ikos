#include <cstdlib>

extern "C" {
extern int* unknown_ptr();
extern bool unknown_flag();

void require_true(bool condition) {
  if (!condition) std::abort();
}

void require_false(bool condition) {
  if (condition) std::abort();
}

void require_pointer(int* pointer) {
  if (pointer == nullptr) std::abort();
}

void require_either(bool left, bool right) {
  if (!left && !right) std::abort();
}

void pointer_wrapper(int* pointer) {
  require_true(pointer != nullptr);
}

void nested_wrapper(int* pointer) {
  pointer_wrapper(pointer);
}

void direct_guard() {
  int* pointer = unknown_ptr();
  require_pointer(pointer);
  *pointer = 42;
}

void boolean_wrapper() {
  int* pointer = unknown_ptr();
  nested_wrapper(pointer);
  *pointer = 42;
}

void reversed_boolean() {
  int* pointer = unknown_ptr();
  require_false(pointer == nullptr);
  *pointer = 42;
}

void either_guard() {
  int* pointer = unknown_ptr();
  require_either(pointer != nullptr, unknown_flag());
  *pointer = 42; // Must warn: the other condition can permit a null pointer.
}

void missing_guard() {
  int* pointer = unknown_ptr();
  *pointer = 42;
}

void incomplete_boolean_origin() {
  int* pointer = unknown_ptr();
  bool condition = unknown_flag();
  if (pointer != nullptr) condition = true;
  require_true(condition);
  *pointer = 42; // Must warn: the initial value can be true when pointer is null.
}

void null_actual() {
  int* pointer = nullptr;
  require_pointer(pointer);
  *pointer = 42; // Unreachable.
}

void require_both(bool left, bool right) {
  require_true(left);
  require_true(right);
}

void both_guard() {
  int* left = unknown_ptr();
  int* right = unknown_ptr();
  require_both(left != nullptr, right != nullptr);
  *left = 1;
  *right = 2;
}

void pointer_either_wrapper(int* pointer, bool condition) {
  require_either(pointer != nullptr, condition);
}

void pointer_either_guard() {
  int* pointer = unknown_ptr();
  pointer_either_wrapper(pointer, unknown_flag());
  *pointer = 42; // A pointer-valued DNF summary must also preserve this warning.
}
}
