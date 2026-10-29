/*******************************************************************************
 *
 * \file
 * \brief Parameter-symbolic summary integration with the main analysis
 *
 ******************************************************************************/

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <ikos/analyzer/analysis/context.hpp>
#include <ikos/analyzer/analysis/literal.hpp>
#include <ikos/analyzer/analysis/parameter_symbolic.hpp>
#include <ikos/analyzer/analysis/variable.hpp>
#include <ikos/analyzer/util/log.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_fixpoint.hpp>

namespace ikos {
namespace analyzer {

namespace {

using frontend::parameter_symbolic::SymbolicCondition;
using frontend::parameter_symbolic::SymbolicConstraint;
using frontend::parameter_symbolic::SymbolicExpr;

struct CallerLinearForm {
  std::map< Variable*, ZNumber > coefficients;
  ZNumber constant = ZNumber(0);
};

bool add_symbolic_expr(CallerLinearForm& form,
                       const SymbolicExpr& expr,
                       int multiplier,
                       Context& ctx,
                       ar::CallBase* call) {
  form.constant += ZNumber(multiplier) * ZNumber(expr.constant);

  for (const auto& term : expr.coeffs) {
    int index = term.first;
    if (index < 0 ||
        static_cast< std::size_t >(index) >= call->num_arguments()) {
      return false;
    }

    ar::Value* argument = call->argument(static_cast< unsigned >(index));
    if (!argument->type()->is_integer())
      return false;

    const ScalarLit& literal = ctx.lit_factory->get_scalar(argument);
    ZNumber coefficient = ZNumber(multiplier) * ZNumber(term.second);
    if (literal.is_machine_int()) {
      form.constant += coefficient * literal.machine_int().to_z_number();
    } else if (literal.is_machine_int_var()) {
      form.coefficients[literal.var()] += coefficient;
      if (form.coefficients[literal.var()] == 0) {
        form.coefficients.erase(literal.var());
      }
    } else {
      return false;
    }
  }
  return true;
}

core::machine_int::Predicate machine_predicate(
    ar::Comparison::Predicate predicate) {
  using AP = ar::Comparison;
  using MP = core::machine_int::Predicate;
  switch (predicate) {
    case AP::SIEQ:
    case AP::UIEQ:
      return MP::EQ;
    case AP::SINE:
    case AP::UINE:
      return MP::NE;
    case AP::SIGT:
    case AP::UIGT:
      return MP::GT;
    case AP::SIGE:
    case AP::UIGE:
      return MP::GE;
    case AP::SILT:
    case AP::UILT:
      return MP::LT;
    case AP::SILE:
    case AP::UILE:
      return MP::LE;
    default:
      ikos_unreachable("non-integer summary predicate");
  }
}

core::machine_int::Predicate reverse_predicate(
    core::machine_int::Predicate predicate) {
  using P = core::machine_int::Predicate;
  switch (predicate) {
    case P::EQ:
      return P::EQ;
    case P::NE:
      return P::NE;
    case P::GT:
      return P::LT;
    case P::GE:
      return P::LE;
    case P::LT:
      return P::GT;
    case P::LE:
      return P::GE;
  }
  ikos_unreachable("unexpected machine integer predicate");
}

bool eval_constant_predicate(const ZNumber& left,
                             core::machine_int::Predicate predicate,
                             const ZNumber& right) {
  using P = core::machine_int::Predicate;
  switch (predicate) {
    case P::EQ:
      return left == right;
    case P::NE:
      return left != right;
    case P::GT:
      return left > right;
    case P::GE:
      return left >= right;
    case P::LT:
      return left < right;
    case P::LE:
      return left <= right;
  }
  ikos_unreachable("unexpected machine integer predicate");
}

enum class AtomResult { Applied, Tautology, Contradiction };

AtomResult apply_pointer_nullity(value::MemoryAbstractDomain& state,
                                 ar::Value* pointer,
                                 bool non_null,
                                 Context& ctx) {
  if (!pointer->type()->is_pointer())
    return AtomResult::Tautology;
  const ScalarLit& literal = ctx.lit_factory->get_scalar(pointer);
  if (literal.is_null()) {
    return non_null ? AtomResult::Contradiction : AtomResult::Tautology;
  }
  if (!literal.is_pointer_var())
    return AtomResult::Tautology;
  if (non_null)
    state.nullity_assert_non_null(literal.var());
  else
    state.nullity_assert_null(literal.var());
  return state.is_bottom() ? AtomResult::Contradiction : AtomResult::Applied;
}

AtomResult apply_boolean_nullity(value::MemoryAbstractDomain& state,
                                 const SymbolicConstraint& constraint,
                                 Context& ctx,
                                 ar::CallBase* call) {
  int index;
  bool truth;
  using namespace frontend::parameter_symbolic;
  if (!boolean_constraint_value(constraint, index, truth) || index < 0 ||
      static_cast< std::size_t >(index) >= call->num_arguments()) {
    return AtomResult::Tautology;
  }
  NullityBoolOrigin origin =
      find_nullity_bool_origin(call->argument(static_cast< unsigned >(index)),
                               call->parent()->code(),
                               call->parent());
  if (!origin.pointer)
    return AtomResult::Tautology;
  return apply_pointer_nullity(state,
                               origin.pointer,
                               truth ? origin.non_null : !origin.non_null,
                               ctx);
}

bool is_direct_unsigned_operand(const SymbolicExpr& expr) {
  return expr.coeffs.empty() ||
         (expr.coeffs.size() == 1 && expr.coeffs.begin()->second == 1 &&
          expr.constant == 0);
}

using TypedExpr = frontend::parameter_symbolic::ConditionExpr;
using CallerExpr = core::machine_int::Expression< Variable* >;
using IntInterval = core::machine_int::Interval;

CallerExpr translate_expression(const TypedExpr& e,
                                value::MemoryAbstractDomain& state,
                                Context& ctx,
                                ar::CallBase* call) {
  if (!e)
    return {};
  if (e.node().kind == TypedExpr::Constant)
    return CallerExpr::constant(e.node().constant);
  if (e.node().kind == TypedExpr::Variable) {
    int i = e.node().variable;
    if (i < 0 || static_cast< std::size_t >(i) >= call->num_arguments())
      return {};
    auto* arg = call->argument(static_cast< unsigned >(i));
    if (!arg->type()->is_integer())
      return {};
    const auto& lit = ctx.lit_factory->get_scalar(arg);
    if (lit.is_machine_int())
      return CallerExpr::constant(
          lit.machine_int().cast(e.bit_width(), e.sign()));
    if (!lit.is_machine_int_var())
      return {};
    return CallerExpr::cast(state.range_expression(lit.var()),
                            e.bit_width(),
                            e.sign());
  }
  auto l = translate_expression(e.left(), state, ctx, call);
  if (e.node().kind == TypedExpr::Cast)
    return CallerExpr::cast(l, e.bit_width(), e.sign());
  return CallerExpr::binary(e.node().op,
                            l,
                            translate_expression(e.right(), state, ctx, call));
}

AtomResult apply_typed(value::MemoryAbstractDomain& state,
                       const SymbolicConstraint& c,
                       Context& ctx,
                       ar::CallBase* call) {
  auto l = translate_expression(c.typed_left, state, ctx, call);
  auto r = translate_expression(c.typed_right, state, ctx, call);
  if (!l || !r)
    return AtomResult::Tautology;
  auto p = machine_predicate(c.predicate);
  if (l.is_constant() && r.is_constant())
    return eval_constant_predicate(l.node().constant.to_z_number(),
                                   p,
                                   r.node().constant.to_z_number())
               ? AtomResult::Tautology
               : AtomResult::Contradiction;
  if (l.is_variable() && r.is_constant())
    state.int_add(p, l.node().variable, r.node().constant);
  else if (l.is_constant() && r.is_variable())
    state.int_add(reverse_predicate(p), r.node().variable, l.node().constant);
  else if (l.is_variable() && r.is_variable())
    state.int_add(p, l.node().variable, r.node().variable);
  if (state.is_bottom())
    return AtomResult::Contradiction;
  if (l.is_constant()) {
    std::swap(l, r);
    p = reverse_predicate(p);
  }
  if (!r.is_constant())
    return AtomResult::Tautology;
  auto range = l.evaluate([&](Variable* v, uint64_t, core::Signedness) {
    return state.int_to_interval(v);
  });
  if (range.is_bottom())
    return AtomResult::Contradiction;
  ZNumber lo = range.lb().to_z_number(), hi = range.ub().to_z_number();
  ZNumber n = r.node().constant.to_z_number();
  using P = core::machine_int::Predicate;
  switch (p) {
    case P::EQ:
      lo = std::max(lo, n);
      hi = std::min(hi, n);
      break;
    case P::NE:
      if (lo == n)
        ++lo;
      if (hi == n)
        --hi;
      break;
    case P::GE:
      lo = std::max(lo, n);
      break;
    case P::GT:
      lo = std::max(lo, ZNumber(n + 1));
      break;
    case P::LE:
      hi = std::min(hi, n);
      break;
    case P::LT:
      hi = std::min(hi, ZNumber(n - 1));
      break;
  }
  if (lo > hi)
    return AtomResult::Contradiction;
  state.refine_expression(l,
                          IntInterval(MachineInt(lo, l.bit_width(), l.sign()),
                                      MachineInt(hi, l.bit_width(), l.sign())));
  return state.is_bottom() ? AtomResult::Contradiction : AtomResult::Applied;
}

AtomResult apply_atom(value::MemoryAbstractDomain& state,
                      const SymbolicConstraint& constraint,
                      Context& ctx,
                      ar::CallBase* call) {
  if (constraint.kind == SymbolicConstraint::KTyped)
    return apply_typed(state, constraint, ctx, call);
  if (constraint.kind == SymbolicConstraint::KUnknown)
    return AtomResult::Tautology;
  if (constraint.kind == SymbolicConstraint::KTrue) {
    return AtomResult::Tautology;
  }
  if (constraint.kind == SymbolicConstraint::KFalse)
    return AtomResult::Contradiction;
  if (constraint.kind == SymbolicConstraint::KNullity) {
    int index = constraint.pointer_parameter;
    if (index < 0 ||
        static_cast< std::size_t >(index) >= call->num_arguments()) {
      return AtomResult::Tautology;
    }
    return apply_pointer_nullity(state,
                                 call->argument(static_cast< unsigned >(index)),
                                 constraint.non_null,
                                 ctx);
  }

  if (constraint.predicate >= ar::Comparison::_BeginUnsignedIntegerPred &&
      constraint.predicate <= ar::Comparison::_EndUnsignedIntegerPred &&
      (!is_direct_unsigned_operand(constraint.left) ||
       !is_direct_unsigned_operand(constraint.right))) {
    // Moving affine terms across an unsigned comparison is not generally
    // equivalent in finite-width arithmetic. Keep only direct operands.
    return AtomResult::Tautology;
  }

  CallerLinearForm form;
  if (!add_symbolic_expr(form, constraint.left, 1, ctx, call) ||
      !add_symbolic_expr(form, constraint.right, -1, ctx, call)) {
    return AtomResult::Tautology;
  }

  core::machine_int::Predicate predicate =
      machine_predicate(constraint.predicate);

  if (form.coefficients.empty()) {
    return eval_constant_predicate(form.constant, predicate, ZNumber(0))
               ? AtomResult::Tautology
               : AtomResult::Contradiction;
  }

  if (form.coefficients.size() == 1) {
    Variable* variable = form.coefficients.begin()->first;
    ZNumber coefficient = form.coefficients.begin()->second;
    if (coefficient != 1 && coefficient != -1) {
      return AtomResult::Tautology;
    }
    if (coefficient == -1)
      predicate = reverse_predicate(predicate);

    auto* type = ar::cast< ar::IntegerType >(variable->type());
    MachineInt constant(-form.constant * coefficient,
                        type->bit_width(),
                        type->sign());
    state.int_add(predicate, variable, constant);
    return AtomResult::Applied;
  }

  if (form.coefficients.size() == 2 && form.constant == 0) {
    auto it = form.coefficients.begin();
    Variable* left = it->first;
    ZNumber left_coefficient = it->second;
    ++it;
    Variable* right = it->first;
    ZNumber right_coefficient = it->second;

    if (left_coefficient == 1 && right_coefficient == -1) {
      state.int_add(predicate, left, right);
      return AtomResult::Applied;
    }
    if (left_coefficient == -1 && right_coefficient == 1) {
      state.int_add(reverse_predicate(predicate), left, right);
      return AtomResult::Applied;
    }
  }

  return AtomResult::Tautology;
}

void prune_flow(value::MemoryAbstractDomain& flow,
                const SymbolicCondition& condition,
                Context& ctx,
                ar::CallBase* call) {
  if (flow.is_bottom() || condition.is_true())
    return;
  if (condition.is_false()) {
    flow.set_to_bottom();
    return;
  }

  value::MemoryAbstractDomain base = flow;
  value::MemoryAbstractDomain result = base;
  result.set_to_bottom();

  for (const SymbolicCondition::Conjunction& conjunction :
       condition.disjuncts) {
    value::MemoryAbstractDomain branch = base;
    for (const SymbolicConstraint& constraint : conjunction) {
      AtomResult atom = apply_atom(branch, constraint, ctx, call);
      if (atom != AtomResult::Contradiction && !branch.is_bottom()) {
        atom = apply_boolean_nullity(branch, constraint, ctx, call);
      }
      if (atom == AtomResult::Contradiction) {
        branch.set_to_bottom();
        break;
      }
      if (branch.is_bottom())
        break;
    }
    result.join_with(std::move(branch));
  }

  flow = std::move(result);
}

bool proves_memory_preconditions(
    Context& ctx,
    const value::MemoryAbstractDomain& entry,
    ar::CallBase* call,
    const frontend::parameter_symbolic::FunctionSummary& summary) {
  if (summary.memory_preconditions.empty())
    return true;
  if (entry.is_bottom())
    return false;
  for (const auto& requirement : summary.memory_preconditions) {
    if (requirement.parameter < 0 ||
        static_cast< std::size_t >(requirement.parameter) >=
            call->num_arguments() ||
        !requirement.type || requirement.type->bit_width() % 8 != 0)
      return false;
    const auto& literal = ctx.lit_factory->get_scalar(
        call->argument(static_cast< unsigned >(requirement.parameter)));
    if (!literal.is_pointer_var() ||
        !entry.uninit_is_initialized(literal.var()) ||
        !entry.nullity_is_non_null(literal.var()))
      return false;
    auto target = entry.pointer_to_points_to(literal.var()).singleton();
    auto offset = entry.pointer_offset_to_interval(literal.var()).singleton();
    if (!target || !offset || !offset->is_zero())
      return false;
    // First supported discharge: a live, exactly typed stack scalar at offset
    // zero. Unknown/global/heap targets are not assumed writable by
    // non-nullity.
    auto* local = dyn_cast< LocalMemoryLocation >(*target);
    if (!local || local->local_var()->type()->pointee() != requirement.type ||
        !entry.lifetime_to_lifetime(local).is_allocated())
      return false;
    uint64_t alignment = std::max(uint64_t(1), requirement.alignment);
    if (local->local_var()->alignment() < alignment ||
        local->local_var()->alignment() % alignment != 0)
      return false;
    auto size = entry.int_to_interval(ctx.var_factory->get_alloc_size(local));
    if (size.is_bottom() ||
        size.lb().to_z_number() < ZNumber(requirement.type->bit_width() / 8))
      return false;
  }
  return true;
}

} // end anonymous namespace

void build_and_store_function_summary(Context& ctx, ar::Function* function) {
  if (function == nullptr || !function->is_definition() ||
      ctx.function_summaries.has(function)) {
    return;
  }

  std::ostringstream trace;
  std::ostream* output = ctx.opts.trace_ar_statements ? &trace : nullptr;

  frontend::parameter_symbolic::FunctionSummary summary =
      frontend::parameter_symbolic::analyze_function(function,
                                                     &ctx.function_summaries,
                                                     output,
                                                     output,
                                                     function->name());
  ctx.function_summaries.put(function, std::move(summary));

  if (ctx.opts.trace_ar_statements) {
    auto msg = log::msg();
    msg << trace.str();
  }
}

void apply_caller_summary(Context& ctx,
                          value::AbstractDomain& invariant,
                          ar::CallBase* call,
                          ar::Function* callee,
                          const value::MemoryAbstractDomain& call_entry) {
  const frontend::parameter_symbolic::FunctionSummary* summary =
      ctx.function_summaries.get(callee);
  if (summary == nullptr)
    return;

  const bool memory_proved =
      proves_memory_preconditions(ctx, call_entry, call, *summary);
  if (ctx.opts.trace_ar_statements && !summary->memory_preconditions.empty()) {
    auto message = log::msg();
    message << "Summary memory preconditions [" << callee->name() << "]: "
            << (memory_proved ? "proved at call entry" : "not proved; skipped")
            << "\n";
  }
  if (!memory_proved) {
    if (ctx.opts.trace_ar_statements)
      log::debug(
          "Skipping caller summary: memory preconditions not proved for '" +
          callee->name() + "'");
    return;
  }

  if (ctx.opts.trace_ar_statements) {
    bool complete =
        (summary->normal.available && summary->normal.complete) ||
        (summary->exception.available && summary->exception.complete);
    log::debug(
        std::string(complete
                        ? "Applying caller summary for function '"
                        : "Skipping partial caller summary for function '") +
        callee->name() + "'");
  }

  if (summary->normal.available && summary->normal.complete) {
    if (!summary->normal.has_exit) {
      invariant.set_normal_flow_to_bottom();
    } else {
      prune_flow(invariant.normal(), summary->normal.condition, ctx, call);
    }
  }

  if (summary->exception.available && summary->exception.complete) {
    if (!summary->exception.has_exit) {
      invariant.set_caught_exceptions_to_bottom();
    } else {
      prune_flow(invariant.caught_exceptions(),
                 summary->exception.condition,
                 ctx,
                 call);
    }
  }
}

} // end namespace analyzer
} // end namespace ikos
