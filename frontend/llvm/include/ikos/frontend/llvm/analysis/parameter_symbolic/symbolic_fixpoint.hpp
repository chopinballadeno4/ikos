#pragma once

#include <ostream>
#include <string>

#include <ikos/ar/semantic/code.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/core/fixpoint/fwd_fixpoint_iterator.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// WTO-based forward fixpoint for one function body.
class SymbolicFunctionFixpoint final
    : public core::InterleavedFwdFixpointIterator< ar::Code*, SymbolicState > {
private:
  using Base =
      core::InterleavedFwdFixpointIterator< ar::Code*, SymbolicState >;

  ar::Function* _fun;

  // Trace output: nullptr = disabled.
  std::ostream* _trace_out;
  // If non-empty, only trace this function by name.
  std::string _trace_fn;

public:
  explicit SymbolicFunctionFixpoint(ar::Function* fun,
                                    std::ostream* trace_out = nullptr,
                                    const std::string& trace_fn = "");

  // --- InterleavedFwdFixpointIterator interface ---

  // Run transfer functions for each statement in bb.
  // When trace is active, prints per-statement state after each exec.
  SymbolicState analyze_node(ar::BasicBlock* bb, SymbolicState pre) override;

  // Edge transfer: identity (path-insensitive).
  SymbolicState analyze_edge(ar::BasicBlock* src,
                             ar::BasicBlock* dst,
                             SymbolicState pre) override;

  // Widening: join (our symbolic domain converges in <=2 iterations).
  SymbolicState extrapolate(ar::BasicBlock* head,
                            unsigned iteration,
                            const SymbolicState& before,
                            const SymbolicState& after) override;

  // Narrowing: identity.
  SymbolicState refine(ar::BasicBlock* head,
                       unsigned iteration,
                       const SymbolicState& before,
                       const SymbolicState& after) override;

  // Stop after 1 narrowing step.
  bool is_decreasing_iterations_fixpoint(ar::BasicBlock* head,
                                         unsigned iteration,
                                         const SymbolicState& before,
                                         const SymbolicState& after) override;

  // No-ops (required by ForwardFixpointIterator).
  void process_pre(ar::BasicBlock* /*bb*/,
                   const SymbolicState& /*pre*/) override {}
  void process_post(ar::BasicBlock* /*bb*/,
                    const SymbolicState& /*post*/) override {}

  // Build a structured FunctionSummary from the fixpoint results.
  FunctionSummary build_summary() const;

  // C.4 summary output: delegates to build_summary().dump(o).
  void dump_summary(std::ostream& o) const;
};

// Analyze one function, print its summary, and return the FunctionSummary.
FunctionSummary analyze_function(ar::Function* fun,
                                 std::ostream& o,
                                 std::ostream* trace_out = nullptr,
                                 const std::string& trace_fn = "");

// Analyze all functions in a bundle, print summaries, and return a SummaryStore.
SummaryStore analyze_bundle(ar::Bundle* bundle,
                            std::ostream& o,
                            std::ostream* trace_out = nullptr,
                            const std::string& trace_fn = "");

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
