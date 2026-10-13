#include <ikos/frontend/llvm/analysis/parameter_symbolic/caller_pruning.hpp>

#include <ikos/ar/semantic/value.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// θ_c substitution
// ---------------------------------------------------------------------------

SymbolicValue subst_expr_into_caller(
    const SymbolicExpr& callee_expr,
    const std::vector< SymbolicValue >& arg_vals) {
  SymbolicExpr result = SymbolicExpr::from_const(callee_expr.constant);

  for (const auto& kv : callee_expr.coeffs) {
    int idx = kv.first;
    int coeff = kv.second;
    if (coeff == 0) continue;
    if (idx < 0 || static_cast< std::size_t >(idx) >= arg_vals.size())
      return SymbolicValue::top();
    const SymbolicValue& arg = arg_vals[static_cast< std::size_t >(idx)];
    if (!arg.is_expr()) return SymbolicValue::top();
    result = result + (arg.expr * coeff);
  }

  return SymbolicValue::from_expr(result);
}

SymbolicValue subst_val_into_caller(
    const SymbolicValue& callee_val,
    ar::Function* callee,
    const std::vector< SymbolicValue >& arg_vals) {
  if (callee_val.is_top()) return SymbolicValue::top();

  if (callee_val.is_expr()) {
    return subst_expr_into_caller(callee_val.expr, arg_vals);
  }

  // KLoc: translate the parameter-relative location to caller context.
  const SymbolicLoc& L = callee_val.loc;

  // Find the index of L.base among callee parameters.
  int param_idx = -1;
  int scan = 0;
  for (auto it = callee->param_begin(); it != callee->param_end(); ++it, ++scan) {
    if (*it == L.base) {
      param_idx = scan;
      break;
    }
  }

  if (param_idx < 0 ||
      static_cast< std::size_t >(param_idx) >= arg_vals.size()) {
    return SymbolicValue::top();
  }

  const SymbolicValue& base_arg =
      arg_vals[static_cast< std::size_t >(param_idx)];
  if (!base_arg.is_loc()) return SymbolicValue::top();

  // Translate the callee offset (linear over callee params) into caller context.
  SymbolicValue offset_val = subst_expr_into_caller(L.offset, arg_vals);
  if (!offset_val.is_expr()) return SymbolicValue::top();

  SymbolicLoc result_loc;
  result_loc.base = base_arg.loc.base;
  result_loc.offset = base_arg.loc.offset + offset_val.expr;
  result_loc.callee_local = false;

  return SymbolicValue::from_loc(result_loc);
}

// ---------------------------------------------------------------------------
// Memory effect application
// ---------------------------------------------------------------------------

void apply_memory_summary(
    SymbolicState& caller_state,
    const std::map< SymbolicLoc, SymbolicValue >& mu_exit,
    ar::Function* callee,
    const std::vector< SymbolicValue >& arg_vals) {
  for (const auto& kv : mu_exit) {
    SymbolicValue target =
        subst_val_into_caller(SymbolicValue::from_loc(kv.first),
                              callee,
                              arg_vals);
    // If we cannot resolve the target location, skip conservatively.
    if (!target.is_loc()) continue;

    SymbolicValue value =
        subst_val_into_caller(kv.second, callee, arg_vals);
    caller_state.mu[target.loc] = value;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

ar::Function* direct_callee(ar::CallBase* call) {
  if (!ar::isa< ar::FunctionPointerConstant >(call->called())) return nullptr;
  return ar::cast< ar::FunctionPointerConstant >(call->called())->function();
}

std::vector< SymbolicValue > capture_args(const SymbolicState& state,
                                          ar::CallBase* call) {
  std::vector< SymbolicValue > arg_vals;
  arg_vals.reserve(call->num_arguments());
  for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
    arg_vals.push_back(state.eval(*it));
  }
  return arg_vals;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Call / Invoke entry points
// ---------------------------------------------------------------------------

void apply_call_summary(SymbolicState& state,
                        ar::Call* call,
                        const SummaryStore& store) {
  // Capture arg values BEFORE exec: exec may overwrite the result variable,
  // and in pathological SSA we want the pre-call values.
  ar::Function* callee = direct_callee(call);
  std::vector< SymbolicValue > arg_vals;
  const FunctionSummary* summary = nullptr;

  if (callee != nullptr) {
    summary = store.get(callee);
    if (summary != nullptr && summary->normal.has_exit &&
        !summary->normal.mu_exit.empty()) {
      arg_vals = capture_args(state, call);
    }
  }

  state.exec(call);

  if (summary != nullptr && !arg_vals.empty()) {
    apply_memory_summary(state,
                         summary->normal.mu_exit,
                         callee,
                         arg_vals);
  }
}

void apply_invoke_memory_summary(
    SymbolicState& state,
    ar::Invoke* invoke,
    bool is_normal_edge,
    const SummaryStore& store,
    const std::vector< SymbolicValue >& arg_vals) {
  ar::Function* callee = direct_callee(invoke);
  if (callee == nullptr) return;

  const FunctionSummary* summary = store.get(callee);
  if (summary == nullptr) return;

  const std::map< SymbolicLoc, SymbolicValue >& mu_exit =
      is_normal_edge ? summary->normal.mu_exit : summary->exception.mu_exit;
  bool has_exit =
      is_normal_edge ? summary->normal.has_exit : summary->exception.has_exit;

  if (has_exit && !mu_exit.empty()) {
    apply_memory_summary(state, mu_exit, callee, arg_vals);
  }
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
