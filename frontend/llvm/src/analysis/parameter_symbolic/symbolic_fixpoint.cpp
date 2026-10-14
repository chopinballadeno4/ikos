#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_fixpoint.hpp>

#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ikos/ar/semantic/bundle.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/caller_pruning.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// SymbolicFunctionFixpoint
// ---------------------------------------------------------------------------

SymbolicFunctionFixpoint::SymbolicFunctionFixpoint(ar::Function* fun,
                                                   const SummaryStore* store,
                                                   std::ostream* trace_out,
                                                   const std::string& trace_fn)
    : Base(fun->body(), SymbolicState::make_bottom()),
      _fun(fun),
      _store(store),
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

    // For direct Call statements: apply callee memory summary from store
    // (apply_call_summary internally calls exec; args captured pre-exec).
    bool via_summary = false;
    if (_store != nullptr) {
      if (auto* call = ar::dyn_cast< ar::Call >(stmt)) {
        apply_call_summary(pre, call, *_store);
        via_summary = true;
      }
    }
    if (!via_summary) {
      pre.exec(stmt);
    }

    if (do_trace) {
      *_trace_out << "         => ";
      pre.dump_compact(*_trace_out);
      *_trace_out << "\n";
    }
    ++idx;
  }
  return pre;
}

SymbolicState SymbolicFunctionFixpoint::analyze_edge(ar::BasicBlock* src,
                                                     ar::BasicBlock* dst,
                                                     SymbolicState pre) {
  // Apply Invoke memory summary on the appropriate edge (normal or exception).
  // In SSA form, an Invoke is always the last statement in its block, so the
  // post-block state is the post-invoke state.  The Invoke only modifies its
  // result variable, never its own arguments, so evaluating args in the
  // post-block state yields the same values as the pre-invoke state.
  if (_store != nullptr && !pre.is_bottom() && !src->empty()) {
    if (auto* invoke = ar::dyn_cast< ar::Invoke >(src->back())) {
      bool is_normal = (dst == invoke->normal_dest());
      std::vector< SymbolicValue > arg_vals;
      arg_vals.reserve(invoke->num_arguments());
      for (auto it = invoke->arg_begin(); it != invoke->arg_end(); ++it) {
        arg_vals.push_back(pre.eval(*it));
      }
      apply_invoke_memory_summary(pre, invoke, is_normal, *_store, arg_vals);
    }
  }
  return pre;
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
// Helper: compute parameter-based path conditions for every block
// ---------------------------------------------------------------------------

using BlockConditions =
    std::unordered_map< ar::BasicBlock*, SymbolicCondition >;

static std::vector< SymbolicValue > capture_symbolic_args(
    const SymbolicState& state,
    ar::CallBase* call) {
  std::vector< SymbolicValue > arg_vals;
  arg_vals.reserve(call->num_arguments());
  for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
    arg_vals.push_back(state.eval(*it));
  }
  return arg_vals;
}

static SymbolicCondition call_exit_condition(
    const SymbolicState& state,
    ar::CallBase* call,
    bool normal,
    const SummaryStore* store) {
  if (store == nullptr ||
      !ar::isa< ar::FunctionPointerConstant >(call->called())) {
    return SymbolicCondition::make_true();
  }

  ar::Function* callee =
      ar::cast< ar::FunctionPointerConstant >(call->called())->function();
  const FunctionSummary* summary = store->get(callee);
  if (summary == nullptr) return SymbolicCondition::make_true();

  const SymbolicCondition* condition = nullptr;
  if (normal) {
    if (!summary->normal.available) return SymbolicCondition::make_true();
    if (!summary->normal.has_exit) return SymbolicCondition::make_false();
    condition = &summary->normal.condition;
  } else {
    if (!summary->exception.available) return SymbolicCondition::make_true();
    if (!summary->exception.has_exit) return SymbolicCondition::make_false();
    condition = &summary->exception.condition;
  }

  return subst_condition_into_caller(*condition,
                                     capture_symbolic_args(state, call));
}

static SymbolicState state_before_last_statement(
    const SymbolicFunctionFixpoint& fp,
    ar::BasicBlock* bb) {
  SymbolicState state = fp.pre(bb);
  if (state.is_bottom() || bb->empty()) return state;

  ar::Statement* last = bb->back();
  for (ar::Statement* stmt : *bb) {
    if (stmt == last) break;
    if (auto* call = ar::dyn_cast< ar::Call >(stmt)) {
      if (fp.summary_store() != nullptr) {
        apply_call_summary(state, call, *fp.summary_store());
      } else {
        state.exec(stmt);
      }
    } else {
      state.exec(stmt);
    }
  }
  return state;
}

static BlockConditions compute_block_conditions(
    const SymbolicFunctionFixpoint& fp,
    ar::Code* body) {
  BlockConditions out_conditions;
  bool changed = true;

  while (changed) {
    changed = false;

    for (ar::BasicBlock* bb : *body) {
      SymbolicCondition condition = SymbolicCondition::make_false();

      if (bb == body->entry_block()) {
        condition = SymbolicCondition::make_true();
      }

      for (auto pit = bb->predecessor_begin();
           pit != bb->predecessor_end(); ++pit) {
        auto it = out_conditions.find(*pit);
        if (it == out_conditions.end()) continue;

        SymbolicCondition predecessor_condition = it->second;
        ar::BasicBlock* predecessor = *pit;
        if (!predecessor->empty()) {
          if (auto* invoke =
                  ar::dyn_cast< ar::Invoke >(predecessor->back())) {
            SymbolicState state =
                state_before_last_statement(fp, predecessor);
            bool normal = (bb == invoke->normal_dest());
            predecessor_condition.meet_with(call_exit_condition(
                state, invoke, normal, fp.summary_store()));
          }
        }
        condition.join_with(predecessor_condition);
      }

      SymbolicState state = fp.pre(bb);
      if (state.is_bottom()) {
        condition = SymbolicCondition::make_false();
      } else {
        for (ar::Statement* stmt : *bb) {
          if (auto* cmp = ar::dyn_cast< ar::Comparison >(stmt)) {
            condition.add_constraint(subst_constraint(cmp, state));
          } else if (auto* call = ar::dyn_cast< ar::Call >(stmt)) {
            condition.meet_with(call_exit_condition(
                state, call, true, fp.summary_store()));
            if (fp.summary_store() != nullptr) {
              apply_call_summary(state, call, *fp.summary_store());
              continue;
            }
          }
          state.exec(stmt);
        }
      }

      auto result = out_conditions.emplace(bb, condition);
      if (result.second || !result.first->second.equals(condition)) {
        result.first->second = std::move(condition);
        changed = true;
      }
    }
  }

  return out_conditions;
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

static bool has_ordinary_call(ar::Code* body) {
  for (ar::BasicBlock* bb : *body) {
    for (ar::Statement* stmt : *bb) {
      if (ar::isa< ar::Call >(stmt)) return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// build_summary: construct FunctionSummary from fixpoint results
// ---------------------------------------------------------------------------

FunctionSummary SymbolicFunctionFixpoint::build_summary() const {
  FunctionSummary summary;
  summary.function = _fun;

  ar::Code* body = _fun->body();
  BlockConditions conditions = compute_block_conditions(*this, body);

  // --- Normal exit ---
  // Once the function analysis completes, absence of a normal CFG exit is a
  // known false normal flow rather than an unavailable summary.
  summary.normal.available = true;
  ar::BasicBlock* nb = body->normal_exit_block_or_null();
  if (nb) {
    const SymbolicState& ns = this->post(nb);
    summary.normal.param_names = ns.param_names;
    summary.normal.condition = conditions[nb];
    summary.normal.has_exit =
        !ns.is_bottom() && !summary.normal.condition.is_false();
    if (summary.normal.has_exit) {
      summary.normal.mu_exit = filter_mu_caller_visible(ns);
    }
  }

  // --- Exception exit ---
  ar::BasicBlock* eb = body->exception_exit_block_or_null();
  if (eb) {
    // The auxiliary symbolic state does not model exceptions propagated by an
    // ordinary Call. If one exists, the explicit exception-exit paths may not
    // cover every exceptional execution, so do not expose a pruning condition.
    summary.exception.available = !has_ordinary_call(body);
    const SymbolicState& es = this->post(eb);
    summary.exception.param_names = es.param_names;
    summary.exception.condition = conditions[eb];
    summary.exception.has_exit =
        !es.is_bottom() && !summary.exception.condition.is_false();
    if (summary.exception.has_exit) {
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
      summary.normal.condition.dump(o, ns.param_names, "    ");
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
      if (summary.exception.available) {
        summary.exception.condition.dump(o, es.param_names, "    ");
      } else {
        o << "    unavailable (ordinary Call exceptions are not modeled)\n";
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
                                 const SummaryStore* store,
                                 std::ostream* summary_out,
                                 std::ostream* trace_out,
                                 const std::string& trace_fn) {
  if (!fun->is_definition()) return FunctionSummary{};

  bool do_trace = (trace_out != nullptr) &&
                  (trace_fn.empty() || fun->name() == trace_fn);
  if (do_trace) {
    *trace_out << "\n=== TRACE: " << fun->name() << " ===\n";
  }

  SymbolicFunctionFixpoint fp(fun, store, trace_out, trace_fn);
  fp.run(SymbolicState::make_entry(fun));
  if (summary_out != nullptr) fp.dump_summary(*summary_out);
  return fp.build_summary();
}

// ---------------------------------------------------------------------------
// Build bottom-up call order (callees before callers) via post-order DFS
// over the direct-call graph.  Recursive / mutually-recursive groups are
// handled conservatively: any cycle edge is just skipped.
// ---------------------------------------------------------------------------

namespace {

// Collect all directly-called defined functions inside one function body.
static std::set< ar::Function* > direct_callees(ar::Function* fun) {
  std::set< ar::Function* > result;
  for (ar::BasicBlock* bb : *fun->body()) {
    for (ar::Statement* stmt : *bb) {
      ar::CallBase* cb = nullptr;
      if (auto* c = ar::dyn_cast< ar::Call >(stmt)) cb = c;
      else if (auto* i = ar::dyn_cast< ar::Invoke >(stmt)) cb = i;
      if (cb && ar::isa< ar::FunctionPointerConstant >(cb->called())) {
        ar::Function* callee =
            ar::cast< ar::FunctionPointerConstant >(cb->called())->function();
        if (callee->is_definition()) result.insert(callee);
      }
    }
  }
  return result;
}

// Post-order DFS: visits callees before callers, writes to `order`.
static void dfs_postorder(ar::Function* fun,
                          std::unordered_set< ar::Function* >& visiting,
                          std::unordered_set< ar::Function* >& visited,
                          std::vector< ar::Function* >& order) {
  if (visited.count(fun)) return;
  if (visiting.count(fun)) return; // back edge (cycle) — skip

  visiting.insert(fun);
  for (ar::Function* callee : direct_callees(fun)) {
    dfs_postorder(callee, visiting, visited, order);
  }
  visiting.erase(fun);
  visited.insert(fun);
  order.push_back(fun);
}

} // anonymous namespace

SummaryStore analyze_bundle(ar::Bundle* bundle,
                            std::ostream& o,
                            std::ostream* trace_out,
                            const std::string& trace_fn) {
  // Build bottom-up order: callee before caller.
  std::unordered_set< ar::Function* > visiting, visited;
  std::vector< ar::Function* > order;

  for (auto it = bundle->function_begin(); it != bundle->function_end(); ++it) {
    ar::Function* fun = *it;
    if (fun->is_definition()) {
      dfs_postorder(fun, visiting, visited, order);
    }
  }

  SummaryStore store;
  for (ar::Function* fun : order) {
    FunctionSummary s = analyze_function(fun, &store, &o, trace_out, trace_fn);
    store.put(fun, std::move(s));
  }
  return store;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
