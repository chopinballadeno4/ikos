// 44: boa — 2D array access guarded by two sequential checks (row, col)
//     each guard computes local vars from params and validates through those local vars
// analysis: boa
// data types: uint32_t
// expected: warning (C_normal complex — local vars used in validation, σ not simplified)

#include <cstdlib>
#include <stdexcept>
#include <cstdint>

extern uint32_t get_row();
extern uint32_t get_col();

static const uint32_t ROWS = 6;
static const uint32_t COLS = 8;

// Row guard: computes back = r*2 - r (= r algebraically), validates through back
// abort(cap==0), throw(back>=cap), return(back<cap)
void check_row(uint32_t r) {
    uint32_t cap    = ROWS; // 6
    uint32_t scaled = r * 2;       // σ(scaled) = 2*p00
    uint32_t back   = scaled - r;  // σ(back)   = complex (2p00 - p00; unsimplified) // r
    if (cap == 0)   std::abort();
    if (back >= cap) throw std::out_of_range("row out of range");
    // C_normal    = {back < 6}  → complex: SMul32(2,p00)-p00 u< 6
    // C_exception = {back >= 6}
    // C_program   = impossible (cap=6 != 0)
}

// Col guard: step1=c+4, step2=step1-4 (=c alg.); validates through step2
// abort(cap==0), throw(step2>=cap), return(step2<cap)
void check_col(uint32_t c) {
    uint32_t cap   = COLS;
    uint32_t step1 = c + 4;       // σ(step1) = p00 + 4
    uint32_t step2 = step1 - 4;   // σ(step2) = complex (SAdd(p00,4)-4; unsimplified) // c
    uint32_t step3 = c * 3;       // σ(step3) = 3*p00  (auxiliary, not used for check)
    (void)step3;
    if (cap == 0)    std::abort();
    if (step2 >= cap) throw std::out_of_range("col out of range");
    // C_normal    = {step2 < 8}  → complex: SSub(SAdd(p00,4),4) u< 8
    // C_exception = {step2 >= 8}
    // C_program   = impossible (cap=8 != 0)
}

// Caller assigns local row/col from params, calls each guard
void cell_guard(uint32_t r_raw, uint32_t c_raw) {
    uint32_t row = r_raw;    // σ(row) = p00
    uint32_t col = c_raw;    // σ(col) = p10
    check_row(row);          // C_normal[check_row]: complex (back<6)
    check_col(col);          // C_normal[check_col]: complex (step2<8)
    // C_normal    = complex expressions in r_raw, c_raw
    // C_exception = ...
    // C_program   = impossible
}

int test() {
    uint32_t r = get_row();
    uint32_t c = get_col();
    int arr[ROWS][COLS] = {};
    cell_guard(r, c);   // C_normal complex → r, c may not be tightly narrowed
    return arr[r][c];   // warning expected: intervals not narrowed by complex C_normal
}

int main() { return test(); }
