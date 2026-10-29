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
    if (coeff == 0)
      continue;
    if (idx < 0 || static_cast< std::size_t >(idx) >= arg_vals.size())
      return SymbolicValue::top();
    const SymbolicValue& arg = arg_vals[static_cast< std::size_t >(idx)];
    if (!arg.is_expr())
      return SymbolicValue::top();
    result = result + (arg.expr * coeff);
  }

  return SymbolicValue::from_expr(result);
}

SymbolicValue subst_val_into_caller(
    const SymbolicValue& callee_val,
    ar::Function* callee,
    const std::vector< SymbolicValue >& arg_vals) {
  if (callee_val.is_top())
    return SymbolicValue::top();
  if (callee_val.is_null())
    return SymbolicValue::null();

  if (callee_val.is_bool()) {
    SymbolicValue pointer =
        subst_val_into_caller(SymbolicValue::from_loc(callee_val.loc),
                              callee,
                              arg_vals);
    if (pointer.is_null()) {
      return SymbolicValue::from_expr(SymbolicExpr::from_const(
                                          callee_val.non_null ? 0 : 1),
                                      true);
    }
    if (pointer.is_loc() && pointer_parameter_index(pointer.loc) >= 0) {
      return SymbolicValue::from_bool(pointer.loc, callee_val.non_null);
    }
    return SymbolicValue::top();
  }

  if (callee_val.is_expr()) {
    return subst_expr_into_caller(callee_val.expr, arg_vals);
  }

  // KLoc: translate the parameter-relative location to caller context.
  const SymbolicLoc& L = callee_val.loc;

  // Find the index of L.base among callee parameters.
  int param_idx = -1;
  int scan = 0;
  for (auto it = callee->param_begin(); it != callee->param_end();
       ++it, ++scan) {
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
  if (base_arg.is_null() && L.offset.is_const() && L.offset.constant == 0) {
    return SymbolicValue::null();
  }
  if (!base_arg.is_loc())
    return SymbolicValue::top();

  // Translate the callee offset (linear over callee params) into caller
  // context.
  SymbolicValue offset_val = subst_expr_into_caller(L.offset, arg_vals);
  if (!offset_val.is_expr())
    return SymbolicValue::top();

  SymbolicLoc result_loc;
  result_loc.base = base_arg.loc.base;
  result_loc.offset = base_arg.loc.offset + offset_val.expr;
  result_loc.callee_local = false;

  return SymbolicValue::from_loc(result_loc);
}

SymbolicCondition subst_condition_into_caller(
    const SymbolicCondition& callee_condition,
    const std::vector< SymbolicValue >& arg_vals,
    const std::vector< ConditionExpr >& typed_args,
    bool* fully_substituted) {
  SymbolicCondition result = SymbolicCondition::make_false();
  if (fully_substituted)
    *fully_substituted = true;
  for (const auto& conjunction : callee_condition.disjuncts) {
    auto translated = SymbolicCondition::make_true();
    for (const auto& c : conjunction) {
      SymbolicConstraint atom = SymbolicConstraint::make_unknown();
      if (c.kind == SymbolicConstraint::KTrue ||
          c.kind == SymbolicConstraint::KFalse)
        atom = c;
      else if (c.kind == SymbolicConstraint::KNullity) {
        int i = c.pointer_parameter;
        if (i >= 0 && static_cast< std::size_t >(i) < arg_vals.size())
          atom = nullity_constraint(arg_vals[static_cast< std::size_t >(i)],
                                    c.non_null);
      } else {
        int bi;
        bool truth;
        if (boolean_constraint_value(c, bi, truth) && bi >= 0 &&
            static_cast< std::size_t >(bi) < arg_vals.size() &&
            arg_vals[static_cast< std::size_t >(bi)].is_bool()) {
          const auto& boolean = arg_vals[static_cast< std::size_t >(bi)];
          atom =
              nullity_constraint(SymbolicValue::from_loc(boolean.loc),
                                 truth ? boolean.non_null : !boolean.non_null);
        } else if (c.kind == SymbolicConstraint::KTyped) {
          auto subst = [&](int i, uint64_t w, core::Signedness sign) {
            if (i >= 0 && static_cast< std::size_t >(i) < typed_args.size() &&
                typed_args[static_cast< std::size_t >(i)])
              return ConditionExpr::cast(typed_args[static_cast< std::size_t >(
                                             i)],
                                         w,
                                         sign);
            if (i >= 0 && static_cast< std::size_t >(i) < arg_vals.size() &&
                arg_vals[static_cast< std::size_t >(i)].is_expr())
              return condition_expr_from_affine(arg_vals[static_cast<
                                                             std::size_t >(i)]
                                                    .expr,
                                                w,
                                                sign);
            return ConditionExpr{};
          };
          atom =
              SymbolicConstraint::make_typed(c.predicate,
                                             c.typed_left.substitute(subst),
                                             c.typed_right.substitute(subst));
        } else if (c.kind == SymbolicConstraint::KParam) {
          auto l = subst_expr_into_caller(c.left, arg_vals),
               r = subst_expr_into_caller(c.right, arg_vals);
          if (l.is_expr() && r.is_expr())
            atom = SymbolicConstraint::make_param(c.predicate, l.expr, r.expr);
        }
      }
      if (atom.kind == SymbolicConstraint::KUnknown) {
        if (fully_substituted)
          *fully_substituted = false;
        translated = SymbolicCondition::make_false();
        break;
      }
      translated.add_constraint(atom);
    }
    result.join_with(translated);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Memory effect application
// ---------------------------------------------------------------------------

void apply_memory_summary(SymbolicState& caller_state,
                          const std::map< SymbolicLoc, SymbolicValue >& mu_exit,
                          ar::Function* callee,
                          const std::vector< SymbolicValue >& arg_vals) {
  std::map< SymbolicLoc, SymbolicValue > translated;
  for (const auto& kv : mu_exit) {
    SymbolicValue target =
        subst_val_into_caller(SymbolicValue::from_loc(kv.first),
                              callee,
                              arg_vals);
    // If we cannot resolve the target location, skip conservatively.
    if (!target.is_loc())
      continue;

    SymbolicValue value = subst_val_into_caller(kv.second, callee, arg_vals);
    auto result = translated.emplace(target.loc, value);
    if (!result.second && result.first->second != value) {
      // Conflicting callee entries alias the same caller location.
      result.first->second = SymbolicValue::top();
    }
  }

  for (const auto& kv : translated) {
    caller_state.mu[kv.first] = kv.second;
  }
}

void invalidate_pointer_arguments(
    SymbolicState& caller_state, const std::vector< SymbolicValue >& arg_vals) {
  for (auto it = caller_state.mu.begin(); it != caller_state.mu.end(); ++it) {
    for (const SymbolicValue& arg : arg_vals) {
      if (arg.is_loc() && it->first.base == arg.loc.base) {
        it->second = SymbolicValue::top();
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

ar::Function* direct_callee(ar::CallBase* call) {
  if (!ar::isa< ar::FunctionPointerConstant >(call->called()))
    return nullptr;
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
  std::vector< SymbolicValue > arg_vals = capture_args(state, call);
  const FunctionSummary* summary = nullptr;

  if (callee != nullptr) {
    summary = store.get(callee);
  }

  state.exec(call);
  invalidate_pointer_arguments(state, arg_vals);

  if (summary == nullptr)
    return;
  // This symbolic state alone cannot prove lifetime, writable extent and
  // alignment. The exit proof and native integration have explicit checks.
  if (!summary->memory_preconditions.empty())
    return;
  if (!summary->normal.available || !summary->normal.complete)
    return;
  if (!summary->normal.has_exit) {
    state.set_to_bottom();
    return;
  }
  if (!summary->normal.mu_exit.empty()) {
    apply_memory_summary(state, summary->normal.mu_exit, callee, arg_vals);
  }
}

void apply_invoke_memory_summary(SymbolicState& state,
                                 ar::Invoke* invoke,
                                 bool is_normal_edge,
                                 const SummaryStore& store,
                                 const std::vector< SymbolicValue >& arg_vals) {
  ar::Function* callee = direct_callee(invoke);
  invalidate_pointer_arguments(state, arg_vals);
  if (callee == nullptr)
    return;

  const FunctionSummary* summary = store.get(callee);
  if (summary == nullptr)
    return;
  if (!summary->memory_preconditions.empty())
    return;

  const std::map< SymbolicLoc, SymbolicValue >& mu_exit =
      is_normal_edge ? summary->normal.mu_exit : summary->exception.mu_exit;
  bool has_exit =
      is_normal_edge ? summary->normal.has_exit : summary->exception.has_exit;
  bool available =
      is_normal_edge ? summary->normal.available : summary->exception.available;

  bool complete =
      is_normal_edge ? summary->normal.complete : summary->exception.complete;
  if (!available || !complete) {
    return;
  } else if (!has_exit) {
    state.set_to_bottom();
  } else if (!mu_exit.empty()) {
    apply_memory_summary(state, mu_exit, callee, arg_vals);
  }
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
