#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>
#include <ikos/ar/semantic/value.hpp>
#include <ikos/core/domain/abstract_domain.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_expr.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// Sigma = (sigma, mu)
//
//   sigma : ar::Variable* -> SymbolicValue  (SSA variable -> expr/loc/TOP)
//   mu    : ar::Variable* -> SymbolicValue  (location var -> stored value)
class SymbolicState final
    : public core::AbstractDomain< SymbolicState > {
public:
  using VarMap = std::unordered_map< ar::Variable*, SymbolicValue >;

  VarMap sigma;
  VarMap mu;
  bool _bottom;

  std::unordered_set< ar::Variable* > local_allocs;
  std::vector< std::string > param_names;

private:
  // Private default constructor — satisfies
  // !std::is_default_constructible<SymbolicState>.
  SymbolicState() : _bottom(false) {}

public:
  // --- Factory ---
  static SymbolicState make_bottom() {
    SymbolicState s;
    s._bottom = true;
    return s;
  }
  static SymbolicState make_entry(ar::Function* fun);

  // --- Copy / Move (public, satisfies is_copy/move_constructible) ---
  SymbolicState(const SymbolicState&) = default;
  SymbolicState(SymbolicState&&) noexcept = default;
  SymbolicState& operator=(const SymbolicState&) = default;
  SymbolicState& operator=(SymbolicState&&) noexcept = default;
  ~SymbolicState() override = default;

  static std::string name() { return "symbolic state"; }

  // --- AbstractDomain interface ---
  void normalize() override {}

  bool is_bottom() const override { return _bottom; }
  bool is_top() const override {
    return !_bottom && sigma.empty() && mu.empty();
  }

  void set_to_bottom() override {
    _bottom = true;
    sigma.clear();
    mu.clear();
  }
  void set_to_top() override {
    _bottom = false;
    sigma.clear();
    mu.clear();
  }

  bool leq(const SymbolicState& other) const override;
  bool equals(const SymbolicState& other) const override {
    return leq(other) && other.leq(*this);
  }

  void join_with(const SymbolicState& other) override;
  void join_with(SymbolicState&& other) override { join_with(other); }

  void widen_with(const SymbolicState& other) override { join_with(other); }
  void meet_with(const SymbolicState& /*other*/) override {}
  void narrow_with(const SymbolicState& /*other*/) override {}

  void dump(std::ostream& o) const override;

  // --- Helpers ---
  SymbolicValue eval(ar::Value* v) const;
  SymbolicValue load_mem(const SymbolicValue& addr) const;
  void store_mem(const SymbolicValue& addr, const SymbolicValue& val);
  void exec(ar::Statement* stmt);

private:
  void exec_assignment(ar::Assignment* s);
  void exec_unary_op(ar::UnaryOperation* s);
  void exec_binary_op(ar::BinaryOperation* s);
  void exec_allocate(ar::Allocate* s);
  void exec_load(ar::Load* s);
  void exec_store(ar::Store* s);
  void exec_call(ar::Call* s);
  void merge_meta(const SymbolicState& other);
};

void dump_state(const SymbolicState& s,
                std::ostream& o,
                const std::string& function_name,
                const std::string& block_name,
                const std::string& label);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
