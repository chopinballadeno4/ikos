#pragma once

#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <ikos/ar/semantic/value.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ─────────────────────────────────────────────
// SymbolicExpr — linear combination over p₀ᵢ
//   Σ aᵢ·p₀ᵢ + c
// ─────────────────────────────────────────────
struct SymbolicExpr {
  std::map<int, int> coeffs; // param_index → coefficient
  int constant = 0;

  static SymbolicExpr from_param(int idx) {
    SymbolicExpr e;
    e.coeffs[idx] = 1;
    return e;
  }
  static SymbolicExpr from_const(int c) {
    SymbolicExpr e;
    e.constant = c;
    return e;
  }

  bool is_const() const { return coeffs.empty(); }

  SymbolicExpr operator+(const SymbolicExpr& o) const {
    SymbolicExpr r = *this;
    r.constant += o.constant;
    for (auto it = o.coeffs.begin(); it != o.coeffs.end(); ++it) {
      r.coeffs[it->first] += it->second;
      if (r.coeffs[it->first] == 0) r.coeffs.erase(it->first);
    }
    return r;
  }
  SymbolicExpr operator-(const SymbolicExpr& o) const {
    return *this + (o * -1);
  }
  SymbolicExpr operator*(int c) const {
    if (c == 0) return from_const(0);
    SymbolicExpr r;
    r.constant = constant * c;
    for (auto it = coeffs.begin(); it != coeffs.end(); ++it)
      r.coeffs[it->first] = it->second * c;
    return r;
  }
  bool operator==(const SymbolicExpr& o) const {
    return coeffs == o.coeffs && constant == o.constant;
  }
  bool operator!=(const SymbolicExpr& o) const { return !(*this == o); }

  void dump(std::ostream& o,
            const std::vector<std::string>& param_names) const;
};

// ─────────────────────────────────────────────
// SymbolicLoc — a symbolic memory location
// ─────────────────────────────────────────────
struct SymbolicLoc {
  ar::Variable* var = nullptr;
  bool callee_local = true; // true = Allocate-created; false = param-relative

  bool operator==(const SymbolicLoc& o) const { return var == o.var; }
  bool operator!=(const SymbolicLoc& o) const { return var != o.var; }
  bool operator<(const SymbolicLoc& o) const { return var < o.var; }
};

// ─────────────────────────────────────────────
// SymbolicValue — Expr | Loc | TOP
// Tagged-struct (C++14 compatible)
// ─────────────────────────────────────────────
struct SymbolicValue {
  enum Kind { KTop, KExpr, KLoc } kind;
  SymbolicExpr expr;
  SymbolicLoc  loc;

  SymbolicValue() : kind(KTop) {}

  static SymbolicValue from_expr(SymbolicExpr e) {
    SymbolicValue v;
    v.kind = KExpr;
    v.expr = std::move(e);
    return v;
  }
  static SymbolicValue from_loc(SymbolicLoc l) {
    SymbolicValue v;
    v.kind = KLoc;
    v.loc = l;
    return v;
  }
  static SymbolicValue top() { return SymbolicValue{}; }

  bool is_top()  const { return kind == KTop; }
  bool is_expr() const { return kind == KExpr; }
  bool is_loc()  const { return kind == KLoc; }

  const SymbolicExpr* get_expr() const { return kind==KExpr ? &expr : nullptr; }
  SymbolicExpr*       get_expr()       { return kind==KExpr ? &expr : nullptr; }
  const SymbolicLoc*  get_loc()  const { return kind==KLoc  ? &loc  : nullptr; }

  bool operator==(const SymbolicValue& o) const {
    if (kind != o.kind) return false;
    if (kind == KExpr) return expr == o.expr;
    if (kind == KLoc)  return loc  == o.loc;
    return true; // both TOP
  }
  bool operator!=(const SymbolicValue& o) const { return !(*this == o); }
};

// Pointwise merge: same → same, different → TOP.
inline SymbolicValue merge_val(const SymbolicValue& a, const SymbolicValue& b) {
  if (a.is_top() || b.is_top()) return SymbolicValue::top();
  if (a == b) return a;
  return SymbolicValue::top();
}

void dump_sym_val(std::ostream& o,
                  const SymbolicValue& v,
                  const std::vector<std::string>& param_names);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
