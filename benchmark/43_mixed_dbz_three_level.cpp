// 43: dbz — three-level call chain; each level passes computed local vars to next
// analysis: dbz
// data types: long
// leaf_check: abort(v<=0), throw(v>200), return(0<v<=200)
// mid_chain: 5-step chain; passes s3 (local var derived from n) to leaf_check
// outer_guard: local vars; passes extra (derived from x) to mid_chain
// test: named d, outer_guard(d), 100/d
// expected: warning (C_normal complex — local vars passed through chain, σ not simplified)

#include <cstdlib>
#include <stdexcept>

extern long get_val();
extern long get_bias();

// Level 1 (innermost): validates v directly
void leaf_check(long v) {
    if (v <= 0)   std::abort();
    if (v > 200L) throw std::range_error("leaf: too large");
    // C_normal    = {v > 0 && v <= 200}
    // C_exception = {v > 200}
    // C_program   = {v <= 0}
}

// Level 2: 5-step chain from params; passes s3 (= n algebraically) to leaf_check
// σ: n=p00, bias=p10
void mid_chain(long n, long bias) {
    long s0 = n + bias;       // σ(s0) = p00 + p10
    long s1 = n * 3;          // σ(s1) = 3*p00
    long s2 = n * 2;          // σ(s2) = 2*p00
    long s3 = s1 - s2;        // σ(s3) = complex (3p00 - 2p00; σ unsimplified)
    long s4 = s3 + bias - s0; // σ(s4) = complex (0 algebraically)
    leaf_check(s3 + s4);      // arg = s3 + 0 = s3 (= n algebraically); passes local var
    // C_normal    = {s3+s4 > 0 && s3+s4 <= 200}  → complex in parameter terms
    // C_exception = {s3+s4 > 200}
    // C_program   = {s3+s4 <= 0}
}

// Level 3: reshapes x into local vars; passes extra (= x algebraically) to mid_chain
// σ: x=p00
void outer_guard(long x) {
    long local_n    = x;               // σ(local_n)    = p00
    long local_bias = local_n * 2;     // σ(local_bias) = 2*p00
    long extra      = local_bias - local_n; // σ(extra) = p00  (= x algebraically)
    mid_chain(extra, local_bias);      // passes extra (local var) as first arg
    // C_normal    = {extra > 0 && extra <= 200}  → complex (σ(extra) unexpanded)
    // C_exception = {extra > 200}
    // C_program   = {extra <= 0}
}

int test() {
    long d    = get_val();
    long bias = get_bias();
    (void)bias;
    outer_guard(d);         // C_normal complex → apply_atom may not fully narrow d
    return (int)(100L / d); // warning expected: d interval not tightly narrowed
}

int main() { return test(); }
