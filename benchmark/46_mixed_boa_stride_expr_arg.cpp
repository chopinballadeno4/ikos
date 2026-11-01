// 46: boa — stride guard with local chain; passes computed local halved (not original step) to inner
// analysis: boa
// data types: unsigned, size_t
// validate_stride: abort(step==0), throw(step>4 or stride>=32), return valid
// stride_guard: computes halved (= step algebraically), passes halved (local var) to validate_stride
//   σ(halved) = complex (2p00 - p00; unsimplified) → C_normal complex on first param
//   stride = halved * base_i = nonlinear → TOP → stride constraint lost
// test: guard(step, base_idx), arr[step * (base_idx+1)]
// expected: warning (C_normal complex + nonlinear stride → cross-constraint lost)

#include <cstdlib>
#include <stdexcept>
#include <cstddef>

extern unsigned get_step();
extern unsigned get_base_idx();

void validate_stride(unsigned step, unsigned stride) {
    if (step == 0) std::abort();
    if (step > 4)  throw std::invalid_argument("step too large");
    if (stride >= 32) throw std::out_of_range("stride overflow");
    // C_normal    = {p00 u!= 0 && p00 u<= 4 && p10 u< 32}
    // C_exception = {(p00 u!= 0 && p10 u>= 32) || (p00 u> 4)}
    // C_program   = {step == 0}
}

// σ: step=p00, idx=p10
void stride_guard(unsigned step, unsigned idx) {
    unsigned doubled = step * 2;            // σ(doubled) = 2*p00
    unsigned halved  = doubled - step;      // σ(halved)  = complex (2p00 - p00; unsimplified)
    unsigned adj_idx = idx + 1;             // σ(adj_idx) = p10 + 1
    unsigned base_i  = adj_idx - 1;        // σ(base_i)  = complex (p10+1-1; unsimplified)
    unsigned stride  = halved * base_i;    // σ(stride) = TOP (complex × complex = nonlinear)
    validate_stride(halved, stride);        // passes halved (local var, σ complex) — not original step
    // C_normal    = {halved != 0 && halved <= 4}  → complex expression in p00 terms
    //   stride < 32 constraint: stride is TOP → lost from C_normal
    // C_exception = {halved != 0 && halved > 4}
    // C_program   = {halved == 0}
}

int test() {
    unsigned step = get_step();
    unsigned bidx = get_base_idx();
    size_t arr[32] = {};
    stride_guard(step, bidx);              // C_normal complex → step not cleanly narrowed
    size_t s = (size_t)step * (bidx + 1); // step upper bound uncertain → s may exceed 31
    return (int)arr[s];                    // warning expected
}

int main() { return test(); }
