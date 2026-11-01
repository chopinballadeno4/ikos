// 45: dbz — selective catch+abort preserves the normal return condition
// analysis: dbz
// data types: int16_t (params), int32_t (locals)
// inner_strict: abort(v==0), throw(v<0), throw(v>300), return(0<v<=300)
// mid_layer: catches "too large" and aborts; other exceptions propagate
//   → C_normal[mid_layer] = {v > 0 && v <= 300}
// test: 100 / d after mid_layer(d) — expected safe
// Exception-type matching may stay partial: no handler path returns normally.
// expected: safe

#include <cstdlib>
#include <stdexcept>
#include <cstdint>

extern int16_t get_val16();

void inner_strict(int32_t v) {
    if (v == 0)   std::abort();
    if (v < 0)    throw std::domain_error("negative");
    if (v > 300)  throw std::range_error("too large");
    // C_normal    = {v > 0 && v <= 300}
    // C_exception = {v < 0 || v > 300}
    // C_program   = {v == 0}
}

// Middle layer: catches range_error (v > 300 path), aborts there; other exceptions propagate
// σ: n=p00
void mid_layer(int32_t n) {
    int32_t wide   = n * 2;          // σ(wide)   = 2*p00
    int32_t narrow = wide - n;       // σ(narrow) = p00
    int32_t capped = narrow;         // σ(capped) = p00
    try {
        inner_strict(capped);        // arg = p00 = n
        // normal return here: n > 0 && n <= 300
    } catch (const std::range_error&) {
        // v > 300 was thrown → n > 300; abort instead of propagating
        std::abort();
    }
    // C_normal    = {n > 0 && n <= 300}
    //   (range_error path → abort; domain_error path still propagates)
    // C_exception = {n < 0}        (only domain_error escapes)
    // C_program   = {n == 0 || n > 300}
}

int test() {
    int32_t d = (int32_t)get_val16();
    mid_layer(d);       // after normal: d in (0, 300]
    return 100 / d;     // safe: d > 0
}

int main() { return test(); }
