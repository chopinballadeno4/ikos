// 41: dbz — auxiliary local ladder inside callee, all three exit types
// analysis: dbz
// strict_check: abort(v==0), throw(v<0 or v>500), return(0<v<=500)
// ladder_guard: 5-step auxiliary chain (not used for validation),
//               calls strict_check(n) with original param directly → C_normal clean
// test: named d passed to ladder_guard, 100/d after normal return
// expected: safe (apply_atom narrows d > 0)

#include <cstdlib>
#include <stdexcept>

extern int get_n();
extern int get_offset();

void strict_check(int v) {
    if (v <= 0)  std::abort();          // zero and negative → program exit
    if (v > 500) throw 1;
    // C_normal    = {v > 0 && v <= 500}   → interval: [1, 500] (no NE, clean GT bound)
    // C_exception = {v > 500}
    // C_program   = {v <= 0}
}

// Auxiliary 5-step chain: computes derived scaled value (for hypothetical logging/accounting)
// Validation uses n directly → C_normal stays clean
void ladder_guard(int n, int offset) {
    int x0 = n + offset;      // aux: offset-inflated; σ(x0) = p00 + p10
    int x1 = x0 * 2;          // aux: doubled;         σ(x1) = 2*p00 + 2*p10
    int x2 = x1 - offset * 2; // aux: cancel offset;   σ(x2) = 2*p00
    int x3 = x2 - n;          // aux: half;            σ(x3) = p00
    int x4 = x3 * 3;          // aux: tripled;         σ(x4) = 3*p00
    (void)x4;                  // auxiliary only — not passed to strict_check
    strict_check(n);           // validate n DIRECTLY: arg = p00 → C_normal clean
    // C_normal    = {n > 0 && n <= 500}   → interval: n in [1,500]
    // C_exception = {n > 500}
    // C_program   = {n <= 0}
}

int test() {
    int d   = get_n();
    int off = get_offset();
    ladder_guard(d, off);   // after normal return: d in (0, 500]
    return 100 / d;         // safe: d > 0
}

int main() { return test(); }
