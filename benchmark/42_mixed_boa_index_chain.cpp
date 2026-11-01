// 42: boa — index bounds guard with 4-step auxiliary chain, throw+abort
// analysis: boa
// data types: ptrdiff_t
// check_index: abort(cap<=0), throw(idx<0 or idx>=cap), return(0<=idx<cap)
// index_chain_guard: 4-step auxiliary chain (computes alignment hints),
//                    validates n DIRECTLY with check_index(n, cap) → C_normal clean
// test: named idx = base+offset, index_chain_guard(idx), arr[idx] safe
// expected: safe (apply_atom narrows idx into [0,9])

#include <cstdlib>
#include <stdexcept>
#include <cstddef>

extern ptrdiff_t get_base();
extern ptrdiff_t get_offset();

void check_index(ptrdiff_t idx, ptrdiff_t cap) {
    if (cap <= 0) std::abort();
    if (idx < 0)    throw std::underflow_error("idx negative");
    if (idx >= cap) throw std::out_of_range("idx out of range");
    // C_normal    = {cap > 0 && idx >= 0 && idx < cap}
    // C_exception = {cap > 0 && (idx < 0 || idx >= cap)}
    // C_program   = {cap <= 0}
}

// 4-step auxiliary chain computes alignment-related metrics (hypothetical use)
// Validation uses n directly → C_normal stays clean as p00 constraints
void index_chain_guard(ptrdiff_t n) {
    ptrdiff_t aligned = n & ~1L;     // aux: floor to even (bitwise, σ = TOP)
    ptrdiff_t offset  = n - aligned; // aux: 0 or 1     (σ = TOP, since aligned is TOP)
    ptrdiff_t scaled  = n * 2;       // aux: byte offset; σ(scaled) = 2*p00
    ptrdiff_t half    = scaled - n;  // aux: back to n;   σ(half)   = p00
    (void)aligned; (void)offset; (void)half;
    ptrdiff_t cap = 10;
    check_index(n, cap);             // validate n DIRECTLY: arg = p00 → C_normal clean
    // C_normal    = {n >= 0 && n < 10}
    // C_exception = {n < 0 || n >= 10}
    // C_program   = impossible (cap=10 > 0)
}

int test() {
    ptrdiff_t base   = get_base();
    ptrdiff_t offset = get_offset();
    ptrdiff_t idx    = base + offset;   // named local
    int arr[10]      = {};
    index_chain_guard(idx);             // after normal return: idx in [0, 9]
    return arr[(int)idx];               // safe: idx >= 0 && idx < 10
}

int main() { return test(); }
