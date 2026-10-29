#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" {
unsigned fresh32();
long long fresh64();
std::size_t fresh_size();
bool fresh_flag();
void opaque_effect();

void range21(long long n) {
  if (n < 1)
    std::abort();
  if (n > 1000)
    std::abort();
  if (n % 2 == 0)
    return;
  if (n > 500)
    std::abort();
}
void positive22(unsigned n) {
  if (n == 0)
    std::abort();
  if (n > 10000)
    std::abort();
}
void division23(long long n) {
  if (n < 2 || n > 1000)
    std::abort();
  long long middle = n / 2;
  if (middle <= 0)
    std::abort();
}
void remainder24(long long n) {
  if (n <= 0 || n > 2000)
    std::abort();
  if (n % 2 != 0 && n > 100)
    std::abort();
}
void index29(std::size_t idx) {
  if (idx < 2 || idx >= 8)
    std::abort();
  std::size_t aligned = idx % 2 == 0 ? idx : idx - 1;
  if (aligned < 2)
    std::abort();
}
void index29_strict(std::size_t idx) {
  if (idx < 2 || idx >= 8)
    std::abort();
  std::size_t aligned = idx % 2 == 0 ? idx : idx - 1;
  if (aligned < 4)
    std::abort();
}
void product30(unsigned step, unsigned idx) {
  if (step == 0 || step > 3 || idx > 3)
    std::abort();
  unsigned product = step * idx;
  if (product > 9)
    std::abort();
}
void product30_strict(unsigned step, unsigned idx) {
  if (step == 0 || step > 3 || idx > 3)
    std::abort();
  unsigned product = step * idx;
  if (product > 4)
    std::abort();
}
void partial_guard(long long n) {
  if (n == 0) {
    opaque_effect();
  } else if (n > 0) {
    std::abort();
  }
}

unsigned repeated_sum() {
  unsigned a = fresh32(), b = fresh32();
  positive22(a + b);
  return 100 / (a + b);
}
void offset28(unsigned n) {
  if (n < 1 || n > 1000)
    std::abort();
}
unsigned backward_subtract() {
  unsigned n = fresh32();
  offset28(n - 1);
  return 100 / n;
}
unsigned backward_constant_minus_variable() {
  unsigned n = fresh32();
  offset28(1001 - n);
  return 100 / n;
}
unsigned backward_division() {
  unsigned n = fresh32();
  offset28(n / 2);
  return 100 / n;
}
unsigned backward_bounded_product() {
  unsigned n = fresh32();
  if (n > 100)
    std::abort();
  offset28(n * 2);
  return 100 / n;
}
unsigned backward_bounded_difference() {
  unsigned a = fresh32(), b = fresh32();
  if (b < 1 || b > 3)
    std::abort();
  offset28(a - b);
  return 100 / a;
}
unsigned backward_add_keeps_zero() {
  unsigned n = fresh32();
  offset28(n + 1);
  return 100 / n;
}
unsigned backward_unknown_difference() {
  unsigned a = fresh32(), b = fresh32();
  offset28(a - b);
  return 100 / a;
}
unsigned backward_wrapped_product_keeps_large() {
  unsigned n = fresh32();
  offset28(n * 2);
  return 100 / (n - 2147483649u);
}
unsigned backward_truncation_keeps_high_bits() {
  unsigned n = fresh32();
  offset28(static_cast< unsigned char >(n));
  return 100 / (n - 257u);
}
unsigned wrapped_sum() {
  unsigned a = UINT32_MAX, b = fresh32();
  positive22(a + b);
  return 100 / (a + b);
}
unsigned unguarded_join() {
  unsigned a = fresh32(), b = fresh32();
  if (fresh_flag())
    positive22(a + b);
  return 100 / (a + b);
}
unsigned changed_operand() {
  unsigned a = fresh32(), b = fresh32();
  positive22(a + b);
  a = fresh32();
  return 100 / (a + b);
}
unsigned different_unknown() {
  unsigned a = fresh32(), b = fresh32();
  positive22(a + b);
  unsigned c = fresh32();
  return 100 / (a + c);
}
unsigned loop_redefinition() {
  unsigned a = fresh32(), b = fresh32();
  positive22(a + b);
  for (unsigned i = 0; i < 2; ++i) {
    a = fresh32();
  }
  return 100 / (a + b);
}
int partial_keeps_zero() {
  long long n = fresh64();
  partial_guard(n);
  return static_cast< int >(100 / n);
}
void partial_throwing_wrapper() {
  opaque_effect();
}
int partial_exception_keeps_error() {
  try {
    partial_throwing_wrapper();
  } catch (...) {
    return static_cast< int >(100 / fresh32());
  }
  return 1;
}
int positive_division() {
  long long n = fresh64();
  division23(n);
  return static_cast< int >(100 / (n - 1));
}
int bounded_index() {
  std::size_t i = fresh_size();
  int array[10] = {};
  index29(i);
  return array[i];
}
int bounded_product() {
  unsigned step = fresh32(), idx = fresh32();
  int array[10] = {};
  product30(step, idx);
  return array[step * idx];
}
int collect_summaries() {
  long long n = fresh64();
  range21(n);
  division23(n);
  remainder24(n);
  std::size_t i = fresh_size();
  index29(i);
  index29_strict(i);
  unsigned step = fresh32(), column = fresh32();
  product30(step, column);
  product30_strict(step, column);
  return 0;
}
}
