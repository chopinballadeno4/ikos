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
// Helper: collect parameter-based constraints from predecessors of an exit
// ---------------------------------------------------------------------------

static void dump_constraints_for_exit(std::ostream& o,
                                      const SymbolicFunctionFixpoint& fp,
                                      ar::BasicBlock* exit_bb,
                                      const std::string& label) {
  // Collect Comparison statements in:
  //   (a) the exit block itself — evaluated with pre(exit_bb)
  //   (b) predecessor blocks  — evaluated with pre(pred)
  // This covers both CFG layouts:
  //   - comparison in the exit block's own entry (as guard on this path)
  //   - comparison in a predecessor that dispatched to this exit
  const std::vector< std::string >& pnames = fp.post(exit_bb).param_names;
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

  // (a) Comparisons inside exit_bb (guard at the top of this block).
  collect_from(exit_bb, fp.pre(exit_bb));

  // (b) Comparisons in predecessors.
  for (auto pit = exit_bb->predecessor_begin();
       pit != exit_bb->predecessor_end(); ++pit) {
    collect_from(*pit, fp.pre(*pit));
  }

  o << "  " << label << " (parameter constraints):\n";
  if (cs.empty()) {
    o << "    true (no expressible constraints found)\n";
  } else {
    for (std::size_t i = 0; i < cs.size(); ++i) {
      o << "    ";
      cs[i].dump(o, pnames);
      o << "\n";
    }
  }
}

// ---------------------------------------------------------------------------
// dump_summary (C.4 output)
// ---------------------------------------------------------------------------

void SymbolicFunctionFixpoint::dump_summary(std::ostream& o) const {
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
      // Return value.
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

      // Caller-visible mu (exclude callee-local allocs).
      o << "  mu_normal_exit (caller-visible):\n";
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

      // Parameter-based constraints.
      dump_constraints_for_exit(o, *this, nb, "C_normal");
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

      dump_constraints_for_exit(o, *this, eb, "C_exception");
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

void analyze_function(ar::Function* fun,
                      std::ostream& o,
                      std::ostream* trace_out,
                      const std::string& trace_fn) {
  if (!fun->is_definition()) return;

  bool do_trace = (trace_out != nullptr) &&
                  (trace_fn.empty() || fun->name() == trace_fn);
  if (do_trace) {
    *trace_out << "\n=== TRACE: " << fun->name() << " ===\n";
  }

  SymbolicFunctionFixpoint fp(fun, trace_out, trace_fn);
  fp.run(SymbolicState::make_entry(fun));
  fp.dump_summary(o);
}

void analyze_bundle(ar::Bundle* bundle,
                    std::ostream& o,
                    std::ostream* trace_out,
                    const std::string& trace_fn) {
  for (auto it = bundle->function_begin(); it != bundle->function_end(); ++it) {
    ar::Function* fun = *it;
    if (fun->is_definition()) {
      analyze_function(fun, o, trace_out, trace_fn);
    }
  }
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
