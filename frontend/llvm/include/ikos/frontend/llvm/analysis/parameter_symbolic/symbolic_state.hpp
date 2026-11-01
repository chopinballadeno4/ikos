#pragma once

#include <map>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>
#include <ikos/ar/semantic/value.hpp>
#include <ikos/core/domain/abstract_domain.hpp>
#include <ikos/core/value/machine_int/expression.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_expr.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

using ConditionExpr = core::machine_int::Expression< int >;

ConditionExpr condition_expr_from_affine(const SymbolicExpr& e,
                                         uint64_t width,
                                         core::Signedness sign);

// Typed modular identities, interpreted over defined executions. Casts and
// non-affine operations remain distinct atoms; failure keeps the original.
ConditionExpr simplify_condition_expr(const ConditionExpr& e);
bool equivalent_condition_expr(const ConditionExpr& left,
                               const ConditionExpr& right);

// ---------------------------------------------------------------------------
// SymbolicState — Sigma = (sigma, mu, rho)
//
//   sigma : ar::Variable* -> SymbolicValue  (SSA var ->
//   expr/pointer/null/Boolean/TOP) mu    : SymbolicLoc -> SymbolicValue
//   (location key -> stored value)
// ---------------------------------------------------------------------------
class SymbolicState final : public core::AbstractDomain< SymbolicState > {
public:
  using VarMap = std::unordered_map< ar::Variable*, SymbolicValue >;
  using MemoryMap = std::map< SymbolicLoc, SymbolicValue >;

  VarMap sigma;
  MemoryMap mu;
  core::machine_int::ExpressionRanges< ar::Variable* > rho;
  bool _bottom;

  // Allocate-created local location variables (callee-local).
  std::unordered_set< ar::Variable* > local_allocs;

  // Parameter names for pretty-printing.
  std::vector< std::string > param_names;

private:
  // Private default constructor — !std::is_default_constructible<SymbolicState>
  SymbolicState() : _bottom(false) {}

public:
  // --- Factories ---
  static SymbolicState make_bottom() {
    SymbolicState s;
    s._bottom = true;
    return s;
  }
  static SymbolicState make_entry(ar::Function* fun);

  // --- Copy / Move (public) ---
  SymbolicState(const SymbolicState&) = default;
  SymbolicState(SymbolicState&&) noexcept = default;
  SymbolicState& operator=(const SymbolicState&) = default;
  SymbolicState& operator=(SymbolicState&&) noexcept = default;
  ~SymbolicState() override = default;

  static std::string name() { return "symbolic state"; }

  // --- AbstractDomain interface ---
  void normalize() override {}

  bool is_bottom() const override { return _bottom; }
  bool is_top() const override;

  void set_to_bottom() override {
    _bottom = true;
    sigma.clear();
    mu.clear();
    rho.clear();
  }
  void set_to_top() override {
    _bottom = false;
    sigma.clear();
    mu.clear();
    rho.clear();
  }

  bool leq(const SymbolicState& other) const override;
  bool equals(const SymbolicState& other) const override {
    return leq(other) && other.leq(*this);
  }

  void join_with(const SymbolicState& other) override;
  void join_with(SymbolicState&& other) override { join_with(other); }

  void widen_with(const SymbolicState& other) override;
  void meet_with(const SymbolicState& /*other*/) override {}
  void narrow_with(const SymbolicState& /*other*/) override {}

  void dump(std::ostream& o) const override;

  // Compact single-line dump for trace output.
  void dump_compact(std::ostream& o) const;

  // --- Helpers used by fixpoint ---
  SymbolicValue eval(ar::Value* v) const;
  SymbolicValue load_mem(const SymbolicValue& addr) const;
  void store_mem(const SymbolicValue& addr, const SymbolicValue& val);
  void exec(ar::Statement* stmt);

private:
  void transfer_ranges(ar::Statement* stmt);
  void exec_assignment(ar::Assignment* s);
  void exec_unary_op(ar::UnaryOperation* s);
  void exec_binary_op(ar::BinaryOperation* s);
  void exec_allocate(ar::Allocate* s);
  void exec_pointer_shift(ar::PointerShift* s);
  void exec_load(ar::Load* s);
  void exec_store(ar::Store* s);
  void exec_call(ar::Call* s);
  void exec_invoke(ar::Invoke* s);
  void exec_extract_element(ar::ExtractElement* s);
  void exec_insert_element(ar::InsertElement* s);
  void exec_shuffle_vector(ar::ShuffleVector* s);

  void merge_meta(const SymbolicState& other);
};

// ---------------------------------------------------------------------------
// SymbolicConstraint — typed parameter comparison/nullity or true/false
// ---------------------------------------------------------------------------
struct SymbolicConstraint {
  enum Kind { KTrue, KFalse, KParam, KNullity, KTyped, KUnknown } kind;
  ar::Comparison::Predicate predicate;   // valid when KParam or KTyped
  SymbolicExpr left, right;              // valid when KParam
  ConditionExpr typed_left, typed_right; // valid when KTyped
  int pointer_parameter = -1;            // valid when KNullity
  bool non_null = true;                  // valid when KNullity

  static SymbolicConstraint make_true() {
    SymbolicConstraint c;
    c.kind = KTrue;
    c.predicate = ar::Comparison::SIEQ; // unused
    return c;
  }
  static SymbolicConstraint make_unknown() {
    SymbolicConstraint c;
    c.kind = KUnknown;
    return c;
  }
  static SymbolicConstraint make_typed(ar::Comparison::Predicate p,
                                       ConditionExpr l,
                                       ConditionExpr r) {
    if (!l || !r)
      return make_unknown();
    l = simplify_condition_expr(l);
    r = simplify_condition_expr(r);
    bool eq = p == ar::Comparison::SIEQ || p == ar::Comparison::UIEQ;
    bool ne = p == ar::Comparison::SINE || p == ar::Comparison::UINE;
    // Equality is bitwise. A same-width sign reinterpretation can be pushed
    // into the constant without assuming anything about the operand's range.
    while ((eq || ne) && l.node().kind == ConditionExpr::Cast &&
           r.is_constant() && l.bit_width() == l.left().bit_width()) {
      l = l.left();
      r = ConditionExpr::constant(
          r.node().constant.cast(l.bit_width(), l.sign()));
      p = l.sign() == core::Signed
              ? (eq ? ar::Comparison::SIEQ : ar::Comparison::SINE)
              : (eq ? ar::Comparison::UIEQ : ar::Comparison::UINE);
    }
    SymbolicConstraint c;
    c.kind = KTyped;
    c.predicate = p;
    c.typed_left = std::move(l);
    c.typed_right = std::move(r);
    return c;
  }
  static SymbolicConstraint make_param(ar::Comparison::Predicate p,
                                       const SymbolicExpr& l,
                                       const SymbolicExpr& r) {
    SymbolicConstraint c;
    c.kind = KParam;
    c.predicate = p;
    c.left = l;
    c.right = r;
    return c;
  }
  static SymbolicConstraint make_false() {
    SymbolicConstraint c;
    c.kind = KFalse;
    return c;
  }
  static SymbolicConstraint make_nullity(int parameter, bool non_null) {
    SymbolicConstraint c;
    c.kind = KNullity;
    c.pointer_parameter = parameter;
    c.non_null = non_null;
    return c;
  }

  void dump(std::ostream& o, const std::vector< std::string >& names) const;

private:
  SymbolicConstraint() : kind(KTrue), predicate(ar::Comparison::SIEQ) {}
};

// Try to substitute sigma into a Comparison, returning a parameter constraint.
// Unsupported atoms are KUnknown and cannot certify a guarantee path.
SymbolicConstraint subst_constraint(ar::Comparison* cmp,
                                    const SymbolicState& s);

// Return the entry pointer index only for a zero-offset formal pointer origin.
int pointer_parameter_index(const SymbolicLoc& pointer);
SymbolicConstraint nullity_constraint(const SymbolicValue& pointer,
                                      bool non_null);

// Check every null-comparison/Boolean-assignment definition, including
// replicated branches. Every path to a non-null use must cross a definition.
// A null use is reserved for normalizing the assignments before merging.
struct NullityBoolOrigin {
  ar::Value* pointer = nullptr;
  bool non_null = true;
};
NullityBoolOrigin find_nullity_bool_origin(ar::Value* boolean,
                                           ar::Code* code,
                                           ar::BasicBlock* use);

// Recognize either operand order of a Boolean ==/!= 0/1 constraint.
bool boolean_constraint_value(const SymbolicConstraint& constraint,
                              int& parameter,
                              bool& truth);

// Detailed per-block dump (used in C.4 output).
void dump_state(const SymbolicState& s,
                std::ostream& o,
                const std::string& function_name,
                const std::string& block_name,
                const std::string& label);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
