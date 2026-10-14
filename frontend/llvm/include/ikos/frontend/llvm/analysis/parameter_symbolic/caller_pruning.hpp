#pragma once

#include <map>
#include <vector>

#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// θ_c — substitute callee-context symbolic values into caller context
//
// arg_vals[i] = θ_c(p_i^0): the caller's SymbolicValue for callee param i,
// computed as state.eval(arg_i) before the call is executed.
// ---------------------------------------------------------------------------

// Substitute a callee SymbolicExpr (Σ a_j·p_j^0 + c) into the caller context.
// Returns KExpr if every referenced param resolves to a scalar expression,
// TOP otherwise.
SymbolicValue subst_expr_into_caller(const SymbolicExpr& callee_expr,
                                     const std::vector< SymbolicValue >& arg_vals);

// Substitute a callee SymbolicValue into the caller context.
// KExpr: scalar substitution via subst_expr_into_caller.
// KLoc:  maps L.base (callee param var) → caller arg loc, then translates offset.
// KTop:  stays TOP.
SymbolicValue subst_val_into_caller(const SymbolicValue& callee_val,
                                    ar::Function* callee,
                                    const std::vector< SymbolicValue >& arg_vals);

// Substitute a DNF exit condition into the caller parameter context.
// An atom with an unrepresentable operand is weakened to true.
SymbolicCondition subst_condition_into_caller(
    const SymbolicCondition& callee_condition,
    const std::vector< SymbolicValue >& arg_vals);

// ---------------------------------------------------------------------------
// Memory effect application
// ---------------------------------------------------------------------------

// Apply callee memory-exit snapshot to the caller symbolic state.
// For each (L → v) in mu_exit:
//   target = θ_c(L)   — translated location in caller context
//   value  = θ_c(v)   — translated value in caller context
//   caller_state.mu[target] = value
// Entries whose target cannot be translated (TOP) are skipped conservatively.
void apply_memory_summary(SymbolicState& caller_state,
                          const std::map< SymbolicLoc, SymbolicValue >& mu_exit,
                          ar::Function* callee,
                          const std::vector< SymbolicValue >& arg_vals);

// Conservatively invalidate symbolic memory reachable through pointer
// arguments. This is performed before applying any known exit-memory relation;
// a missing summary entry never means that memory is unchanged.
void invalidate_pointer_arguments(
    SymbolicState& caller_state,
    const std::vector< SymbolicValue >& arg_vals);

// ---------------------------------------------------------------------------
// Call / Invoke entry points (called from SymbolicFunctionFixpoint)
// ---------------------------------------------------------------------------

// Handle a direct Call statement: exec the call, then apply normal memory
// effects from store (if the callee has a summary).
// arg values are captured internally before exec — do NOT exec separately.
void apply_call_summary(SymbolicState& state,
                        ar::Call* call,
                        const SummaryStore& store);

// Handle memory effects for one edge of a direct Invoke statement.
// arg_vals must be captured from the pre-exec state (before exec_invoke ran).
// is_normal_edge: true → apply normal summary, false → apply exception summary.
void apply_invoke_memory_summary(SymbolicState& state,
                                 ar::Invoke* invoke,
                                 bool is_normal_edge,
                                 const SummaryStore& store,
                                 const std::vector< SymbolicValue >& arg_vals);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
