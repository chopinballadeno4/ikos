#pragma once

#include <map>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <ikos/ar/semantic/function.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_expr.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// NormalSummary
//
// Represents the caller-visible result of a normal (return) exit.
//
//   constraints  : C_normal — conjunction of parameter-based constraints
//                  derived from SubstConstraint at the normal-exit block.
//                  Empty list means "true" (no expressible constraint).
//   mu_exit      : μ_normal_exit — caller-visible memory at normal exit.
//                  Callee-local Allocate locations are excluded.
// ---------------------------------------------------------------------------
struct NormalSummary {
  bool has_exit = false;
  std::vector< SymbolicConstraint > constraints;
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
  bool has_exit = false;
  std::vector< SymbolicConstraint > constraints;
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
  void put(ar::Function* fun, FunctionSummary summary);
  const FunctionSummary* get(ar::Function* fun) const;
  bool has(ar::Function* fun) const;

private:
  std::unordered_map< ar::Function*, FunctionSummary > _store;
};

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
