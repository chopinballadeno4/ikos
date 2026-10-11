#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_fixpoint.hpp>

#include <iostream>

#include <ikos/ar/semantic/bundle.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// SymbolicFunctionFixpoint
// ---------------------------------------------------------------------------

SymbolicFunctionFixpoint::SymbolicFunctionFixpoint(ar::Function* fun,
                                                   std::ostream* trace_out,
                                                   const std::string& trace_fn)
    : Base(fun->body(), SymbolicState::make_bottom()),
      _fun(fun),
      _trace_out(trace_out),
      _trace_fn(trace_fn) {}

SymbolicState SymbolicFunctionFixpoint::analyze_node(ar::BasicBlock* bb,
                                                     SymbolicState pre) {
  if (pre.is_bottom()) return pre;

  // Decide whether to emit trace for this block.
  bool do_trace = (_trace_out != nullptr) &&
                  (_trace_fn.empty() || _fun->name() == _trace_fn);

  if (do_trace) {
    *_trace_out << "  [" << bb->name_or_empty() << "]\n";
  }

  int idx = 0;
  for (ar::Statement* stmt : *bb) {
    if (do_trace) {
      *_trace_out << "    stmt#" << idx << " | ";
      stmt->dump(*_trace_out);
      *_trace_out << "\n";
      *_trace_out << "            ";
      pre.dump_compact(*_trace_out);
      *_trace_out << "\n";
    }

    pre.exec(stmt);

    if (do_trace) {
      *_trace_out << "         => ";
      pre.dump_compact(*_trace_out);
      *_trace_out << "\n";
    }
    ++idx;
  }
  return pre;
}

SymbolicState SymbolicFunctionFixpoint::analyze_edge(ar::BasicBlock* /*src*/,
                                                     ar::BasicBlock* /*dst*/,
                                                     SymbolicState pre) {
  return pre; // path-insensitive
}

SymbolicState SymbolicFunctionFixpoint::extrapolate(
    ar::BasicBlock* /*head*/,
    unsigned /*iteration*/,
    const SymbolicState& before,
    const SymbolicState& after) {
  return before.join_iter(after); // widening = join
}

SymbolicState SymbolicFunctionFixpoint::refine(ar::BasicBlock* /*head*/,
                                               unsigned /*iteration*/,
                                               const SymbolicState& before,
                                               const SymbolicState& /*after*/) {
  return before; // no narrowing
}

bool SymbolicFunctionFixpoint::is_decreasing_iterations_fixpoint(
    ar::BasicBlock* /*head*/,
    unsigned /*iteration*/,
    const SymbolicState& /*before*/,
    const SymbolicState& /*after*/) {
  return true; // stop after one narrowing step (identity)
}

// ---------------------------------------------------------------------------
// Helper: collect parameter-based constraints from an exit block
// ---------------------------------------------------------------------------

// Collects Comparison statements from:
//   (a) the exit block itself — evaluated with pre(exit_bb)
//   (b) predecessor blocks   — evaluated with pre(pred)
// This covers both CFG layouts where the branch condition appears in either
// the exit block or its dispatching predecessor.
static std::vector< SymbolicConstraint > collect_exit_constraints(
    const SymbolicFunctionFixpoint& fp,
    ar::BasicBlock* exit_bb) {
  std::vector< SymbolicConstraint > cs;

  auto collect_from = [&](ar::BasicBlock* bb, const SymbolicState& state) {
    if (state.is_bottom()) return;
    for (ar::Statement* stmt : *bb) {
      if (auto* cmp = ar::dyn_cast< ar::Comparison >(stmt)) {
        SymbolicConstraint sc = subst_constraint(cmp, state);
        if (sc.kind == SymbolicConstraint::KParam) {
          cs.push_back(sc);
        }
      }
    }
  };

  collect_from(exit_bb, fp.pre(exit_bb));
  for (auto pit = exit_bb->predecessor_begin();
       pit != exit_bb->predecessor_end(); ++pit) {
    collect_from(*pit, fp.pre(*pit));
  }
  return cs;
}

// ---------------------------------------------------------------------------
// Helper: filter mu to caller-visible locations (exclude callee-local allocs)
// ---------------------------------------------------------------------------

static std::map< SymbolicLoc, SymbolicValue > filter_mu_caller_visible(
    const SymbolicState& state) {
  std::map< SymbolicLoc, SymbolicValue > result;
  for (const auto& kv : state.mu) {
    if (state.local_allocs.count(kv.first.base) == 0) {
      result[kv.first] = kv.second;
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// build_summary: construct FunctionSummary from fixpoint results
// ---------------------------------------------------------------------------

FunctionSummary SymbolicFunctionFixpoint::build_summary() const {
  FunctionSummary summary;
  summary.function = _fun;

  ar::Code* body = _fun->body();

  // --- Normal exit ---
  ar::BasicBlock* nb = body->normal_exit_block_or_null();
  if (nb) {
    const SymbolicState& ns = this->post(nb);
    summary.normal.has_exit = true;
    summary.normal.param_names = ns.param_names;
    if (!ns.is_bottom()) {
      summary.normal.constraints = collect_exit_constraints(*this, nb);
      summary.normal.mu_exit = filter_mu_caller_visible(ns);
    }
  }

  // --- Exception exit ---
  ar::BasicBlock* eb = body->exception_exit_block_or_null();
  if (eb) {
    const SymbolicState& es = this->post(eb);
    summary.exception.has_exit = true;
    summary.exception.param_names = es.param_names;
    if (!es.is_bottom()) {
      summary.exception.constraints = collect_exit_constraints(*this, eb);
      summary.exception.mu_exit = filter_mu_caller_visible(es);
    }
  }

  // program_exit: not included in caller summary (no return to caller).

  return summary;
}

// ---------------------------------------------------------------------------
// dump_summary: C.4 output — full state + structured summary per exit
// ---------------------------------------------------------------------------

void SymbolicFunctionFixpoint::dump_summary(std::ostream& o) const {
  FunctionSummary summary = build_summary();
  const std::string& fname = _fun->name();

  o << "============================\n";
  o << "Function: " << fname << "\n";
  o << "============================\n";

  ar::Code* body = _fun->body();

  // --- Normal exit ---
  ar::BasicBlock* nb = body->normal_exit_block_or_null();
  if (nb) {
    const SymbolicState& ns = this->post(nb);
    o << "\n[normal_exit] @ " << nb->name_or_empty() << "\n";
    dump_state(ns, o, fname, nb->name_or_empty(), "post");

    if (!ns.is_bottom()) {
      if (!nb->empty()) {
        if (auto* ret = ar::dyn_cast< ar::ReturnValue >(nb->back())) {
          if (ret->has_operand()) {
            o << "  return value = ";
            dump_sym_val(o, ns.eval(ret->operand()), ns.param_names);
            o << "\n";
          } else {
            o << "  return void\n";
          }
        }
      }

      o << "  mu_normal_exit (caller-visible):\n";
      if (summary.normal.mu_exit.empty()) {
        o << "    (empty)\n";
      } else {
        for (const auto& kv : summary.normal.mu_exit) {
          o << "    ";
          dump_sym_loc(o, kv.first, ns.param_names);
          o << " -> ";
          dump_sym_val(o, kv.second, ns.param_names);
          o << "\n";
        }
      }

      o << "  C_normal (parameter constraints):\n";
      if (summary.normal.constraints.empty()) {
        o << "    true\n";
      } else {
        for (const SymbolicConstraint& c : summary.normal.constraints) {
          o << "    ";
          c.dump(o, ns.param_names);
          o << "\n";
        }
      }
    }
  } else {
    o << "\n[normal_exit] none\n";
  }

  // --- Exception exit ---
  ar::BasicBlock* eb = body->exception_exit_block_or_null();
  if (eb) {
    const SymbolicState& es = this->post(eb);
    o << "\n[exception_exit] @ " << eb->name_or_empty() << "\n";
    dump_state(es, o, fname, eb->name_or_empty(), "post");

    if (!es.is_bottom()) {
      o << "  mu_exception_exit (caller-visible):\n";
      if (summary.exception.mu_exit.empty()) {
        o << "    (empty)\n";
      } else {
        for (const auto& kv : summary.exception.mu_exit) {
          o << "    ";
          dump_sym_loc(o, kv.first, es.param_names);
          o << " -> ";
          dump_sym_val(o, kv.second, es.param_names);
          o << "\n";
        }
      }

      o << "  C_exception (parameter constraints):\n";
      if (summary.exception.constraints.empty()) {
        o << "    true\n";
      } else {
        for (const SymbolicConstraint& c : summary.exception.constraints) {
          o << "    ";
          c.dump(o, es.param_names);
          o << "\n";
        }
      }
    }
  } else {
    o << "\n[exception_exit] none\n";
  }

  // --- Program exit ---
  ar::BasicBlock* pb = body->program_exit_block_or_null();
  o << "\n[program_exit] "
    << (pb ? pb->name_or_empty() : "none")
    << " (no summary -- process terminates)\n";

  o << "\n";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FunctionSummary analyze_function(ar::Function* fun,
                                 std::ostream& o,
                                 std::ostream* trace_out,
                                 const std::string& trace_fn) {
  if (!fun->is_definition()) return FunctionSummary{};

  bool do_trace = (trace_out != nullptr) &&
                  (trace_fn.empty() || fun->name() == trace_fn);
  if (do_trace) {
    *trace_out << "\n=== TRACE: " << fun->name() << " ===\n";
  }

  SymbolicFunctionFixpoint fp(fun, trace_out, trace_fn);
  fp.run(SymbolicState::make_entry(fun));
  fp.dump_summary(o);
  return fp.build_summary();
}

SummaryStore analyze_bundle(ar::Bundle* bundle,
                            std::ostream& o,
                            std::ostream* trace_out,
                            const std::string& trace_fn) {
  SummaryStore store;
  for (auto it = bundle->function_begin(); it != bundle->function_end(); ++it) {
    ar::Function* fun = *it;
    if (fun->is_definition()) {
      FunctionSummary s = analyze_function(fun, o, trace_out, trace_fn);
      store.put(fun, std::move(s));
    }
  }
  return store;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
