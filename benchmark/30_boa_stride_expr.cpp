// Test: uint32_t args; stride expression (s = step*idx) computed after guard
// analysis: boa
// expected: arr[s] safe — step in [1,3], idx in [0,3] → s in [0,9]

#include <cstdlib>
#include <cstdint>

extern uint32_t get_step();
extern uint32_t get_idx();

void guard_stride_params(uint32_t step, uint32_t idx) {
    if (step == 0) abort();
    // assume(step != 0)
    if (step > 3) abort();
    // assume(step <= 3)
    if (idx > 4) abort();
    // assume(idx <= 4)
    uint32_t prod = step * idx;
    if (prod > 9) abort();  // cross-check (always satisfied given above bounds)
    // assume(prod <= 9)

    // C_N = {step != 0 && step <= 3 && idx <= 3 && step * idx <= 9}
}

int test(void) {
    uint32_t step = get_step(), idx = get_idx();
    int arr[10] = {};
    guard_stride_params(step, idx);  // step in [1,3], idx in [0,3]
    uint32_t s = step * idx;         // computed after guard: s in [0,9]
    return arr[s];                   // safe
}

int main() { return test(); }
