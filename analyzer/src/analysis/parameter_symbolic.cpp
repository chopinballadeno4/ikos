/*******************************************************************************
 *
 * \file
 * \brief Parameter-symbolic summary integration with the main analysis
 *
 ******************************************************************************/

#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <ikos/analyzer/analysis/context.hpp>
#include <ikos/analyzer/analysis/literal.hpp>
#include <ikos/analyzer/analysis/parameter_symbolic.hpp>
#include <ikos/analyzer/util/log.hpp>
#include <ikos/analyzer/analysis/variable.hpp>
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
    if (!argument->type()->is_integer()) return false;

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
    case P::EQ: return P::EQ;
    case P::NE: return P::NE;
    case P::GT: return P::LT;
    case P::GE: return P::LE;
    case P::LT: return P::GT;
    case P::LE: return P::GE;
  }
  ikos_unreachable("unexpected machine integer predicate");
}

bool eval_constant_predicate(const ZNumber& left,
                             core::machine_int::Predicate predicate,
                             const ZNumber& right) {
  using P = core::machine_int::Predicate;
  switch (predicate) {
    case P::EQ: return left == right;
    case P::NE: return left != right;
    case P::GT: return left > right;
    case P::GE: return left >= right;
    case P::LT: return left < right;
    case P::LE: return left <= right;
  }
  ikos_unreachable("unexpected machine integer predicate");
}

enum class AtomResult { Applied, Tautology, Contradiction };

bool is_direct_unsigned_operand(const SymbolicExpr& expr) {
  return expr.coeffs.empty() ||
         (expr.coeffs.size() == 1 && expr.coeffs.begin()->second == 1 &&
          expr.constant == 0);
}

AtomResult apply_atom(value::MemoryAbstractDomain& state,
                      const SymbolicConstraint& constraint,
                      Context& ctx,
                      ar::CallBase* call) {
  if (constraint.kind == SymbolicConstraint::KTrue) {
    return AtomResult::Tautology;
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
    if (coefficient == -1) predicate = reverse_predicate(predicate);

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
  if (flow.is_bottom() || condition.is_true()) return;
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
      if (atom == AtomResult::Contradiction) {
        branch.set_to_bottom();
        break;
      }
      if (branch.is_bottom()) break;
    }
    result.join_with(std::move(branch));
  }

  flow = std::move(result);
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
                          ar::Function* callee) {
  const frontend::parameter_symbolic::FunctionSummary* summary =
      ctx.function_summaries.get(callee);
  if (summary == nullptr) return;

  if (ctx.opts.trace_ar_statements) {
    log::debug("Applying caller summary for function '" + callee->name() +
               "'");
  }

  if (summary->normal.available) {
    if (!summary->normal.has_exit) {
      invariant.set_normal_flow_to_bottom();
    } else {
      prune_flow(invariant.normal(), summary->normal.condition, ctx, call);
    }
  }

  if (summary->exception.available) {
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
