#pragma once

#include <ikos/ar/semantic/code.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/core/fixpoint/fwd_fixpoint_iterator.hpp>

#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// WTO-based forward fixpoint for one function body.
//
// AbstractValue = SymbolicState  (must satisfy InterleavedFwdFixpointIterator
// requirements: join_with, join_iter, widening, leq, normalize, is_bottom)
class SymbolicFunctionFixpoint final
    : public core::InterleavedFwdFixpointIterator< ar::Code*, SymbolicState > {
private:
  using Base =
      core::InterleavedFwdFixpointIterator< ar::Code*, SymbolicState >;

  ar::Function* _fun;

public:
  explicit SymbolicFunctionFixpoint(ar::Function* fun);

  // Propagate the symbolic state through a basic block.
  SymbolicState analyze_node(ar::BasicBlock* bb, SymbolicState pre) override;

  // Edge transfer: currently identity (branch constraints are path-insensitive).
  SymbolicState analyze_edge(ar::BasicBlock* src,
                             ar::BasicBlock* dst,
                             SymbolicState pre) override;

  // Widening: same as join for our domain.
  SymbolicState extrapolate(ar::BasicBlock* head,
                            unsigned iteration,
                            const SymbolicState& before,
                            const SymbolicState& after) override;

  // Narrowing: identity (no narrowing in our domain).
  SymbolicState refine(ar::BasicBlock* head,
                       unsigned iteration,
                       const SymbolicState& before,
                       const SymbolicState& after) override;

  // Decreasing fixpoint check: stop after 1 narrowing step.
  bool is_decreasing_iterations_fixpoint(ar::BasicBlock* head,
                                         unsigned iteration,
                                         const SymbolicState& before,
                                         const SymbolicState& after) override;

  // Called after computing pre/post invariants per block (no-op here).
  void process_pre(ar::BasicBlock* /*bb*/,
                   const SymbolicState& /*pre*/) override {}
  void process_post(ar::BasicBlock* /*bb*/,
                    const SymbolicState& /*post*/) override {}

  // Print the symbolic summary for the function (C.4 output).
  void dump_summary(std::ostream& o) const;
};

// Top-level entry: analyze one function and print its summary.
void analyze_function(ar::Function* fun, std::ostream& o);

// Analyze all functions in a bundle and print summaries.
void analyze_bundle(ar::Bundle* bundle, std::ostream& o);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
