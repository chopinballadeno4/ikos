#pragma once

#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include <ikos/ar/semantic/function.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_expr.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// SymbolicCondition
//
// Exit condition in disjunctive normal form (DNF):
//
//   (c_11 && ... && c_1n) || ... || (c_m1 && ... && c_mk)
//
// `disjuncts.empty()` means false. A single empty conjunction means true.
//
// Unrepresentable constraints invalidate their conjunction, never weaken it.
// ---------------------------------------------------------------------------
struct SymbolicCondition {
  using Conjunction = std::vector< SymbolicConstraint >;

  std::vector< Conjunction > disjuncts;

  static SymbolicCondition make_false();
  static SymbolicCondition make_true();

  bool is_false() const { return disjuncts.empty(); }
  bool is_true() const;

  /// \brief Add a path alternative (logical OR).
  void join_with(const SymbolicCondition& other);

  /// \brief Conjoin another DNF condition (logical AND).
  void meet_with(const SymbolicCondition& other);

  /// \brief Add a constraint to every path (logical AND).
  void add_constraint(const SymbolicConstraint& constraint);

  bool equals(const SymbolicCondition& other) const;

  void dump(std::ostream& o,
            const std::vector< std::string >& param_names,
            const std::string& indent) const;
};

// ---------------------------------------------------------------------------
// NormalSummary
//
// Represents the caller-visible result of a normal (return) exit.
// Callee-local Allocate locations are excluded from mu_exit.
// ---------------------------------------------------------------------------
struct NormalSummary {
  bool available = false;
  bool has_exit = false;
  // False means a sufficient condition for only part of the input space.
  // In particular false/Partial is NOT proof that this flow is impossible.
  bool complete = false;
  SymbolicCondition condition = SymbolicCondition::make_false();
  std::map< SymbolicLoc, SymbolicValue > mu_exit;
  std::vector< std::string > param_names;

  void dump(std::ostream& o) const;
};

// ---------------------------------------------------------------------------
// ExceptionSummary
//
// Represents the caller-visible result of an exception-propagation exit.
// Structure mirrors NormalSummary.
// ---------------------------------------------------------------------------
struct ExceptionSummary {
  bool available = false;
  bool has_exit = false;
  bool complete = false;
  SymbolicCondition condition = SymbolicCondition::make_false();
  std::map< SymbolicLoc, SymbolicValue > mu_exit;
  std::vector< std::string > param_names;

  void dump(std::ostream& o) const;
};

// ---------------------------------------------------------------------------
// FunctionSummary
//
// Per-function summary produced after the symbolic fixpoint analysis.
// program_exit paths are excluded: they never return to the caller.
// ---------------------------------------------------------------------------
struct FunctionSummary {
  ar::Function* function = nullptr;
  NormalSummary normal;
  ExceptionSummary exception;

  void dump(std::ostream& o) const;
};

// ---------------------------------------------------------------------------
// SummaryStore
//
// Registry that maps each analyzed function to its FunctionSummary.
// Used during interprocedural analysis so callers can look up callee summaries.
// ---------------------------------------------------------------------------
class SummaryStore {
public:
  SummaryStore() = default;
  SummaryStore(const SummaryStore&) = delete;
  SummaryStore& operator=(const SummaryStore&) = delete;
  SummaryStore(SummaryStore&& other) noexcept;
  SummaryStore& operator=(SummaryStore&& other) noexcept;

  /// \brief Store a summary unless one already exists for the function.
  void put(ar::Function* fun, FunctionSummary summary);
  const FunctionSummary* get(ar::Function* fun) const;
  bool has(ar::Function* fun) const;

private:
  // std::map keeps references stable when another function is inserted.
  std::map< ar::Function*, FunctionSummary > _store;
  mutable std::mutex _mutex;
};

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
