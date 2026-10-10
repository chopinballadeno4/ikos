#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_fixpoint.hpp>

#include <iostream>

#include <ikos/ar/semantic/bundle.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// SymbolicFunctionFixpoint
// ---------------------------------------------------------------------------

SymbolicFunctionFixpoint::SymbolicFunctionFixpoint(ar::Function* fun)
    : Base(fun->body(), SymbolicState::make_bottom()), _fun(fun) {}

SymbolicState SymbolicFunctionFixpoint::analyze_node(ar::BasicBlock* bb,
                                                     SymbolicState pre) {
  if (pre.is_bottom()) return pre;
  for (ar::Statement* stmt : *bb) {
    pre.exec(stmt);
  }
  return pre;
}

SymbolicState SymbolicFunctionFixpoint::analyze_edge(ar::BasicBlock* /*src*/,
                                                     ar::BasicBlock* /*dst*/,
                                                     SymbolicState pre) {
  // Path-insensitive: branch conditions don't refine symbolic state.
  return pre;
}

SymbolicState SymbolicFunctionFixpoint::extrapolate(
    ar::BasicBlock* /*head*/,
    unsigned /*iteration*/,
    const SymbolicState& before,
    const SymbolicState& after) {
  // Widening = join: our domain converges to TOP in ≤ 2 iterations.
  return before.join_iter(after);
}

SymbolicState SymbolicFunctionFixpoint::refine(ar::BasicBlock* /*head*/,
                                               unsigned /*iteration*/,
                                               const SymbolicState& before,
                                               const SymbolicState& /*after*/) {
  // No narrowing: return before unchanged.
  return before;
}

bool SymbolicFunctionFixpoint::is_decreasing_iterations_fixpoint(
    ar::BasicBlock* /*head*/,
    unsigned /*iteration*/,
    const SymbolicState& /*before*/,
    const SymbolicState& /*after*/) {
  // Stop immediately: one narrowing step (identity) is enough.
  return true;
}

// ---------------------------------------------------------------------------
// dump_summary (C.4 output)
// ---------------------------------------------------------------------------

void SymbolicFunctionFixpoint::dump_summary(std::ostream& o) const {
  const std::string& fname = _fun->name();
  o << "════════════════════════════════════════\n";
  o << "Function: " << fname << "\n";
  o << "════════════════════════════════════════\n";

  ar::Code* body = _fun->body();

  // --- Normal exit ---
  ar::BasicBlock* nb = body->normal_exit_block_or_null();
  if (nb) {
    const SymbolicState& ns = this->post(nb);
    o << "\n[normal_exit]\n";
    dump_state(ns, o, fname, nb->has_name() ? nb->name() : "?", "post");
    // Summarize σ at normal exit.
    if (!ns.is_bottom()) {
      o << "  Return σ mapping (parameter-origin):\n";
      // Look for the ReturnValue statement's operand.
      if (!nb->empty()) {
        if (auto* ret = ar::dyn_cast<ar::ReturnValue>(nb->back())) {
          o << "    return value = ";
          dump_sym_val(o, ns.eval(ret->operand()), ns.param_names);
          o << "\n";
        }
      }
      // Caller-visible μ (external locations only).
      o << "  μ_normal_exit (caller-visible):\n";
      bool any = false;
      for (auto it2 = ns.mu.begin(); it2 != ns.mu.end(); ++it2) {
        ar::Variable* var = it2->first;
        const SymbolicValue& val = it2->second;
        if (ns.local_allocs.count(var)) continue;
        o << "    L[";
        if (var->has_name()) o << var->name();
        o << "] -> ";
        dump_sym_val(o, val, ns.param_names);
        o << "\n";
        any = true;
      }
      if (!any) o << "    (empty)\n";
    }
  } else {
    o << "\n[normal_exit] none\n";
  }

  // --- Exception exit ---
  ar::BasicBlock* eb = body->exception_exit_block_or_null();
  if (eb) {
    const SymbolicState& es = this->post(eb);
    o << "\n[exception_exit]\n";
    dump_state(es, o, fname, eb->has_name() ? eb->name() : "?", "post");
    if (!es.is_bottom()) {
      o << "  μ_exception_exit (caller-visible):\n";
      bool any = false;
      for (auto it2 = es.mu.begin(); it2 != es.mu.end(); ++it2) {
        ar::Variable* var = it2->first;
        const SymbolicValue& val = it2->second;
        if (es.local_allocs.count(var)) continue;
        o << "    L[";
        if (var->has_name()) o << var->name();
        o << "] -> ";
        dump_sym_val(o, val, es.param_names);
        o << "\n";
        any = true;
      }
      if (!any) o << "    (empty)\n";
    }
  } else {
    o << "\n[exception_exit] none\n";
  }

  // --- Program exit ---
  ar::BasicBlock* pb = body->program_exit_block_or_null();
  o << "\n[program_exit] " << (pb ? (pb->has_name() ? pb->name() : "?") : "none")
    << " (no summary — process terminates)\n";

  o << "\n";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void analyze_function(ar::Function* fun, std::ostream& o) {
  if (!fun->is_definition()) return;

  SymbolicFunctionFixpoint fp(fun);
  fp.run(SymbolicState::make_entry(fun));
  fp.dump_summary(o);
}

void analyze_bundle(ar::Bundle* bundle, std::ostream& o) {
  for (auto it = bundle->function_begin(); it != bundle->function_end(); ++it) {
    ar::Function* fun = *it;
    if (fun->is_definition()) {
      analyze_function(fun, o);
    }
  }
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
