// 47: dbz — parameter modified inside callee; passes computed local (not original) to inner check
// analysis: dbz
// data types: int64_t
// check_positive: abort(v<=0), throw(v>1000), return(0<v<=1000)
// param_modify_guard: builds chain from n,extra; passes 'delta' (local var) to check_positive
//   σ(delta) = complex (cannot simplify to p00) → C_normal complex → apply_atom imprecise
// test: named d = get_val64(), param_modify_guard(d, extra), 100/d
// expected: warning (C_normal complex → d not tightly narrowed to >0)

#include <cstdlib>
#include <stdexcept>
#include <cstdint>

extern int64_t get_val64();
extern int64_t get_extra();

void check_positive(int64_t v) {
    if (v <= 0)    std::abort();
    if (v > 1000L) throw std::range_error("too large");
    // C_normal    = {v > 0 && v <= 1000}
    // C_exception = {v > 1000}
    // C_program   = {v <= 0}
}

// n is modified; chain builds shifted→doubled→back→delta; passes delta (local var) to check_positive
// σ: n=p00, extra=p10
void param_modify_guard(int64_t n, int64_t extra) {
    int64_t shifted = n + extra;            // σ(shifted) = p00 + p10
    n               = shifted;              // σ(n) = p00 + p10   (param modified)
    int64_t doubled = n * 2;               // σ(doubled) = 2*(p00+p10)
    int64_t back    = doubled - n;         // σ(back)    = complex (2*(p00+p10)-(p00+p10); unsimplified)
    int64_t delta   = back - extra;        // σ(delta)   = complex (p00+p10-p10; unsimplified)
      // back = doubled - n = 2n - n = n
      // delta = n - extra
    check_positive(delta);                  // passes delta (local var, σ complex) — not original n
    // C_normal    = {delta > 0 && delta <= 1000}  → complex expression in p00, p10 terms
    // C_exception = {delta > 1000}
    // C_program   = {delta <= 0}
}

int test() {
    int64_t d     = get_val64();
    int64_t extra = get_extra();
    param_modify_guard(d, extra);   // C_normal complex → d not narrowed to >0 by apply_atom
    return (int)(100LL / d);        // warning expected: d could be 0 or negative
}

int main() { return test(); }
