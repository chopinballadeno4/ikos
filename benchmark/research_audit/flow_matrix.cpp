// Reproducible research audit. Run run_audit.py, not main alone.
// Inputs are read before try so each catch receives only the guard's exception.
// Most guards have normal, exception and program-exit inputs.
// BAD_NORMAL/BAD_EXCEPTION create independently checked negative controls.
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

struct AuditError {
  int reason;
  explicit AuditError(int n) : reason(n) {}
  ~AuditError() {}
};
struct ClearingError {
  int* target;
  explicit ClearingError(int* p) : target(p) {}
  ~ClearingError() { *target = 0; }
};
struct TerminatingError {
  int code;
  TerminatingError() : code(1) {}
  ~TerminatingError() { std::abort(); }
};

#ifndef BAD_NORMAL
#define BAD_NORMAL 0
#endif
#ifndef BAD_EXCEPTION
#define BAD_EXCEPTION 0
#endif

extern "C" {
int32_t fresh_i32();
int16_t fresh_i16();
int8_t fresh_i8();
uint8_t fresh_u8();
uint16_t fresh_u16();
uint32_t fresh_u32();
uint64_t fresh_u64();
int64_t fresh_i64();
bool fresh_bool();
float fresh_float();
double fresh_double();
int* fresh_pointer();
void opaque_call();
// Match the official ikos/analyzer/intrinsic.h C++ contract.
void __ikos_assert(int) noexcept;

// N: [0,7]; E: [-8,-1]; program exit: outside [-8,7].
void split_i32(int32_t n) {
  if (n < -8 || n > 7) std::abort();
  if (n < 0) throw 1;
}
void split_i16(int16_t n) { split_i32(n); }
void split_i8(int8_t n) { split_i32(n); }

int audit_dbz_i8() {
  int8_t n = fresh_i8();
  try {
    split_i8(n);
    return 100 / (BAD_NORMAL ? n : n + 1); // AUDIT: dbz8_n safe
  } catch (...) {
    return 100 / (BAD_EXCEPTION ? n + 1 : -n); // AUDIT: dbz8_e safe
  }
}

void typed_wrapper(int32_t n) {
  try {
    split_i32(n);
  } catch (int) {
    throw;
  }
}
int audit_typed_rethrow() {
  int32_t n = fresh_i32();
  char normal[8] = {}, recovery[8] = {};
  try {
    typed_wrapper(n);
    return normal[n + (BAD_NORMAL ? 8 : 0)]; // AUDIT: typed_n safe
  } catch (...) {
    return recovery[n + (BAD_EXCEPTION ? 0 : 8)]; // AUDIT: typed_e safe
  }
}

int audit_dbz_i16() {
  int16_t n = fresh_i16();
  try {
    split_i16(n);
    return 100 / (BAD_NORMAL ? n : n + 1); // AUDIT: dbz_n safe
  } catch (...) {
    return 100 / (BAD_EXCEPTION ? n + 1 : -n); // AUDIT: dbz_e safe
  }
}
int audit_shift_i16() {
  int16_t n = fresh_i16();
  try {
    split_i16(n);
    return 1u << (BAD_NORMAL ? n + 32 : n); // AUDIT: shift_n safe
  } catch (...) {
    return 1u << (BAD_EXCEPTION ? n : n + 8); // AUDIT: shift_e safe
  }
}
int audit_sio_i32() {
  int32_t n = fresh_i32();
  try {
    split_i32(n);
    return n + (BAD_NORMAL ? INT32_MAX : INT32_MAX - 7); // AUDIT: sio_n safe
  } catch (...) {
    return n + (BAD_EXCEPTION ? INT32_MIN : INT32_MIN + 8); // AUDIT: sio_e safe
  }
}

// N: [1,4]; E: [5,8]; program exit: zero or >8.
void split_u8(uint8_t n) {
  if (n == 0 || n > 8) std::abort();
  if (n > 4) throw 2;
}
uint32_t audit_uio_u8() {
  uint8_t n = fresh_u8();
  try {
    split_u8(n);
    return uint32_t(n) + (BAD_NORMAL ? UINT32_MAX : UINT32_MAX - 4); // AUDIT: uio_n safe
  } catch (...) {
    return uint32_t(n) + (BAD_EXCEPTION ? UINT32_MAX : UINT32_MAX - 8); // AUDIT: uio_e safe
  }
}

// N: sum in [0,7]; E: sum in [8,15]; remaining sums abort.
void split_sum64(uint64_t sum) {
  if (sum >= 16) std::abort();
  if (sum >= 8) throw 3;
}
void sum_wrapper64(uint64_t sum) {
  uint64_t restored = (sum + 11) - 11;
  split_sum64(restored);
}
int audit_boa_sum64() {
  uint64_t a = fresh_u64(), b = fresh_u64();
  char normal[8] = {}, recovery[8] = {};
  try {
    sum_wrapper64(a + b);
    return normal[(a + b) + (BAD_NORMAL ? 8 : 0)]; // AUDIT: sum_n safe
  } catch (...) {
    return recovery[(a + b) - (BAD_EXCEPTION ? 0 : 8)]; // AUDIT: sum_e safe
  }
}
int audit_prover_sum64() {
  uint64_t a = fresh_u64(), b = fresh_u64();
  try {
    sum_wrapper64(a + b);
    __ikos_assert(BAD_NORMAL ? a + b >= 8 : a + b < 8); // AUDIT: prover_n safe
  } catch (...) {
    __ikos_assert(BAD_EXCEPTION ? a + b < 8 : (a + b >= 8 && a + b < 16)); // AUDIT: prover_e safe
  }
  return 0;
}

// N: n/2 in [0,15]; E: n/2 in [16,31]; remaining n abort.
void split_quotient64(int64_t n) {
  if (n < 0 || n > 63) std::abort();
  if (n / 2 >= 16) throw 4;
}
int audit_boa_div64() {
  int64_t n = fresh_i64();
  char normal[16] = {}, recovery[16] = {};
  try {
    split_quotient64(n);
    return normal[n / 2 + (BAD_NORMAL ? 16 : 0)]; // AUDIT: quotient_n safe
  } catch (...) {
    return recovery[n / 2 - (BAD_EXCEPTION ? 0 : 16)]; // AUDIT: quotient_e safe
  }
}

// N: positive n with n%3 != 0; E: positive multiples of 3.
void split_remainder16(int16_t n) {
  if (n < 1 || n > 30) std::abort();
  if (n % 3 == 0) throw 5;
}
int audit_dbz_rem16() {
  int16_t n = fresh_i16();
  try {
    split_remainder16(n);
    return 100 / ((n % 3) - (BAD_NORMAL ? 1 : 0)); // AUDIT: rem_n safe
  } catch (...) {
    return 100 / ((n % 3) + (BAD_EXCEPTION ? 0 : 1)); // AUDIT: rem_e safe
  }
}

// Opposite nullity facts; pointers target initialized locals in the caller.
void split_pointers(int* primary, int* backup) {
  if (primary == nullptr) {
    if (backup == nullptr) std::abort();
    throw 6;
  }
  if (backup != nullptr) std::abort();
}
int audit_nullity() {
  int first = 1, second = 2;
  int* primary = fresh_bool() ? &first : nullptr;
  int* backup = fresh_bool() ? &second : nullptr;
  try {
    split_pointers(primary, backup);
    return *(BAD_NORMAL ? backup : primary); // AUDIT: pointer_n safe
  } catch (...) {
    return *(BAD_EXCEPTION ? primary : backup); // AUDIT: pointer_e safe
  }
}
void split_null_bool(bool absent) {
  if (absent) throw 7;
}
// Broader precision probe: the pointer itself has two AR definitions (PHI).
// The current DEV_SUMMARY section 9 explicitly excludes this shape.
int audit_bool_origin_phi() {
  int value = 1;
  int* pointer = fresh_bool() ? &value : nullptr;
  try {
    split_null_bool(pointer == nullptr);
    __ikos_assert(pointer != nullptr); // AUDIT: bool_phi_n safe
  } catch (...) {
    __ikos_assert(pointer == nullptr); // AUDIT: bool_phi_e safe
  }
  return 0;
}
int audit_bool_origin() {
  int* pointer = fresh_pointer(); // one definition: supported Boolean origin
  try {
    split_null_bool(pointer == nullptr);
    __ikos_assert(BAD_NORMAL ? pointer == nullptr : pointer != nullptr); // AUDIT: bool_n safe
  } catch (...) {
    __ikos_assert(BAD_EXCEPTION ? pointer != nullptr : pointer == nullptr); // AUDIT: bool_e safe
  }
  return 0;
}

void split_class(int32_t n) {
  if (n < -8 || n > 7) std::abort();
  if (n < 0) throw AuditError(n);
}
void class_wrapper(int32_t n) { split_class(n); }
int audit_class_bounds() {
  int32_t n = fresh_i32();
  char normal[8] = {}, recovery[8] = {};
  try {
    class_wrapper(n);
    return normal[n + (BAD_NORMAL ? 8 : 0)]; // AUDIT: class_n safe
  } catch (...) {
    return recovery[n + (BAD_EXCEPTION ? 0 : 8)]; // AUDIT: class_e safe
  }
}
void split_standard_class(int32_t n) {
  if (n < -8 || n > 7) std::abort();
  if (n < 0) throw std::range_error("negative");
}
int audit_standard_class() {
  int32_t n = fresh_i32();
  char normal[8] = {}, recovery[8] = {};
  try {
    split_standard_class(n);
    return normal[n + (BAD_NORMAL ? 8 : 0)]; // AUDIT: stdclass_n safe
  } catch (...) {
    return recovery[n + (BAD_EXCEPTION ? 0 : 8)]; // AUDIT: stdclass_e safe
  }
}
int audit_destructor_invalidation() {
  int n = 1;
  try {
    throw ClearingError(&n);
  } catch (...) {
  } // destructor clears n here; a previous nonzero fact is invalid now
  return 100 / n; // AUDIT: destructor_effect detect
}
int audit_destructor_program_exit(int n) {
  try {
    throw TerminatingError();
  } catch (...) {
  } // destructor aborts: the following statement cannot execute
  return 100 / n; // AUDIT: destructor_unreachable unreachable
}

// Writable(out) is required; uninitialized pointee storage is valid at entry.
void split_memory(int mode, int* out) {
  if (mode < 0 || mode > 1) std::abort();
  if (mode == 0) {
    if (!BAD_NORMAL) *out = 7;
    return;
  }
  if (!BAD_EXCEPTION) *out = 9;
  throw 8;
}
int audit_memory() {
  int mode = fresh_i32();
  int value;
  try {
    split_memory(mode, &value);
    return 100 / value; // AUDIT: memory_n safe
  } catch (...) {
    return 100 / value; // AUDIT: memory_e safe
  }
}

// Wrapped uint32 multiplication is deliberately different from uint64 math.
void split_product32(uint32_t step, uint32_t index) {
  if (step == 0 || step > 4) std::abort();
  if (step * index >= 16) throw 9;
}
int audit_wrapped_product() {
  uint32_t step = fresh_u32(), index = fresh_u32();
  char normal[16] = {};
  try {
    split_product32(step, index);
    // Safe only with the exact uint32_t product, not its widened counterpart.
    uint64_t offset = BAD_NORMAL ? uint64_t(step) * index : uint32_t(step * index);
    return normal[offset]; // AUDIT: product_n safe
  } catch (...) {
    return 0;
  }
}

// Deliberate precision limit: signed !=0 is an interval with an interior hole.
void nonzero_guard(int32_t n) {
  if (n == 0) std::abort();
}
void clear_then_throw(int* out) {
  *out = 0;
  throw 13;
}
int audit_memory_invalidation() {
  int n = fresh_i32();
  // A prior nonzero fact must not survive a store on the exception path.
  nonzero_guard(n);
  try {
    clear_then_throw(&n);
  } catch (...) {
    return 100 / n; // AUDIT: invalidated_memory detect
  }
  return 0;
}

int audit_redefinition() {
  uint64_t a = fresh_u64(), b = fresh_u64();
  char normal[8] = {}, recovery[8] = {};
  try {
    sum_wrapper64(a + b);
    a = 8 - b;
    return normal[a + b]; // AUDIT: redefined_n detect
  } catch (...) {
    a = 16 - b;
    return recovery[a + b - 8]; // AUDIT: redefined_e detect
  }
}

// Truncation must not transfer uint8_t bounds to the uint32_t source.
void split_byte16(uint8_t n) {
  if (n >= 16) std::abort();
  if (n >= 8) throw 14;
}
int audit_truncation() {
  uint32_t n = fresh_u32();
  char normal[8] = {}, recovery[8] = {};
  try {
    split_byte16(static_cast<uint8_t>(n));
    return normal[n]; // AUDIT: truncation_n detect
  } catch (...) {
    return recovery[n - 8]; // AUDIT: truncation_e detect
  }
}
int audit_ne_hole() {
  int32_t n = fresh_i32();
  nonzero_guard(n);
  return 100 / n; // AUDIT: ne_hole limitation
}

// Unknown call can return or throw on n<=0. Neither flow may discard n==0.
void unknown_outcome(int32_t n) {
  if (n > 0) throw 10;
  opaque_call();
}
int audit_partial() {
  int32_t n = fresh_i32();
  try {
    unknown_outcome(n);
    return 100 / n; // AUDIT: partial_n detect
  } catch (...) {
    return 100 / (n - 1); // AUDIT: partial_e detect
  }
}

// Native overflow BEFORE a guard must remain observable.
int audit_overflow_before_guard() {
  int n = fresh_i32();
  int scaled = n * 3; // AUDIT: overflow_before detect
  split_i32(n);
  return scaled;
}

// IEEE floating arithmetic is deliberately a capability probe, not integer DBZ.
void split_float(float n) {
  if (n == 0.0f) std::abort();
  if (n < 0.0f) throw 11;
}
double audit_float_probe() {
  float n = fresh_float();
  double d = fresh_double();
  try {
    split_float(n);
    return d / n; // AUDIT: float_n unsupported
  } catch (...) {
    return d / n; // AUDIT: float_e unsupported
  }
}

// Combined six-parameter contract; two boxes, opposite pointer facts and
// distinct caller-visible stores. Mixed boxes abort. No summary return value.
void route_packet(int32_t n, uint64_t amount, unsigned row,
                  int* primary, int* backup, int* out) {
  if (n < -8 || n > 7 || amount >= 16 || row >= 8) std::abort();
  if (n >= 0) {
    if (amount >= 8 || row >= 4 || primary == nullptr || backup != nullptr)
      std::abort();
    *out = 7;
    return;
  }
  if (amount < 8 || row < 4 || primary != nullptr || backup == nullptr)
    std::abort();
  *out = 9;
  throw 12;
}
void packet_wrapper(int32_t n, uint64_t amount, unsigned row,
                    int* primary, int* backup, int* out) {
  uint64_t restored_amount = (amount + 13) - 13;
  unsigned restored_row = row * 2 - row;
  route_packet(n, restored_amount, restored_row, primary, backup, out);
}
int audit_packet() {
  int32_t n = fresh_i32();
  uint64_t a = fresh_u64(), b = fresh_u64();
  uint16_t row = fresh_u16();
  int first = 3, second = 4, divisor;
  int* primary = fresh_bool() ? &first : nullptr;
  int* backup = fresh_bool() ? &second : nullptr;
  char normal[8] = {}, recovery[8] = {};
  volatile int sink;
  volatile unsigned shift_sink;
  try {
    packet_wrapper(n, a + b, row, primary, backup, &divisor);
    sink = 100 / divisor; // AUDIT: packet_div_n safe
    sink = normal[row]; // AUDIT: packet_row_n safe
    sink = normal[a + b]; // AUDIT: packet_sum_n safe
    sink = *primary; // AUDIT: packet_pointer_n safe
    shift_sink = 1u << n; // AUDIT: packet_shift_n safe
    sink = n + (INT32_MAX - 7); // AUDIT: packet_sio_n safe
    __ikos_assert(divisor == 7); // AUDIT: packet_effect_n safe
  } catch (...) {
    sink = 100 / divisor; // AUDIT: packet_div_e safe
    sink = recovery[row - 4]; // AUDIT: packet_row_e safe
    sink = recovery[a + b - 8]; // AUDIT: packet_sum_e safe
    sink = *backup; // AUDIT: packet_pointer_e safe
    shift_sink = 1u << (n + 8); // AUDIT: packet_shift_e safe
    sink = n + (INT32_MIN + 8); // AUDIT: packet_sio_e safe
    __ikos_assert(divisor == 9); // AUDIT: packet_effect_e safe
  }
  (void)sink;
  (void)shift_sink;
  return 0;
}
}
