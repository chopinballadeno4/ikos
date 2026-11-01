// 50: boa — different 3D coordinate boxes on normal and exception flows.
// analysis: boa
// expected: safe on BOTH accesses; both flows are reachable.
// Normal: row in [0,3], col in [0,5], depth in [0,7].
// Exception: row in [4,7], col in [6,11], depth in [8,15].
// Coordinates outside these two boxes abort, including mixed low/high boxes.

#include <cstdlib>

extern unsigned get_row();
extern unsigned get_col();
extern unsigned get_depth();

void check_cell(unsigned row, unsigned col, unsigned depth) {
  if (row >= 8 || col >= 12 || depth >= 16)
    std::abort();
  if (row < 4) {
    if (col >= 6 || depth >= 8)
      std::abort();
    return;
  }
  if (col < 6 || depth < 8)
    std::abort();
  throw 1;
  // C_normal    = {row < 4 && col < 6 && depth < 8}
  // C_exception = {4 <= row && row < 8 &&
  //                6 <= col && col < 12 && 8 <= depth && depth < 16}
}

void triple_guard(unsigned row, unsigned col, unsigned depth) {
  unsigned shifted_row = row + 4;
  unsigned restored_row = shifted_row - 4;
  unsigned doubled_col = col * 2;
  unsigned restored_col = doubled_col - col;
  unsigned shifted_depth = depth + 1;
  unsigned restored_depth = shifted_depth - 1;
  check_cell(restored_row, restored_col, restored_depth);
  // Each restored coordinate equals its parameter modulo unsigned width.
  // C_normal/C_exception are the two boxes above, both [Complete].
}

int test() {
  // Read before try: the catch handles only exceptions from triple_guard.
  unsigned row = get_row();
  unsigned col = get_col();
  unsigned depth = get_depth();
  int normal[4][6][8] = {};
  int recovery[4][6][8] = {};
  try {
    triple_guard(row, col, depth);
    // Normal summary bounds all three original coordinates.
    return normal[row][col][depth]; // safe
  } catch (...) {
    // Exception summary bounds all three shifted coordinates.
    return recovery[row - 4][col - 6][depth - 8]; // safe
  }
}

int main() { return test(); }
