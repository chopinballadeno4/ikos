#include <ikos/frontend/llvm/analysis/parameter_symbolic/exit_proof.hpp>

#include <algorithm>
#include <map>
#include <vector>

#include <ikos/ar/semantic/code.hpp>
#include <ikos/ar/semantic/intrinsic.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/caller_pruning.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {
namespace {

using Expr = ConditionExpr;
using Interval = core::machine_int::Interval;
using MachineInt = core::MachineInt;
using ZNumber = core::ZNumber;
using Bin = core::machine_int::BinaryOperator;
using Pred = ar::Comparison;

bool equality(Pred::Predicate p) {
  return p == Pred::SIEQ || p == Pred::UIEQ;
}
bool inequality(Pred::Predicate p) {
  return p == Pred::SINE || p == Pred::UINE;
}
bool compare(Pred::Predicate p, const ZNumber& l, const ZNumber& r) {
  switch (p) {
    case Pred::SIEQ:
    case Pred::UIEQ:
      return l == r;
    case Pred::SINE:
    case Pred::UINE:
      return l != r;
    case Pred::SIGT:
    case Pred::UIGT:
      return l > r;
    case Pred::SIGE:
    case Pred::UIGE:
      return l >= r;
    case Pred::SILT:
    case Pred::UILT:
      return l < r;
    case Pred::SILE:
    case Pred::UILE:
      return l <= r;
    default:
      return false;
  }
}

bool binary_operator(ar::BinaryOperation* s, Bin& op) {
  using A = ar::BinaryOperation;
  switch (s->op()) {
    case A::UAdd:
    case A::SAdd:
      op = s->has_no_wrap() ? Bin::AddNoWrap : Bin::Add;
      break;
    case A::USub:
    case A::SSub:
      op = s->has_no_wrap() ? Bin::SubNoWrap : Bin::Sub;
      break;
    case A::UMul:
    case A::SMul:
      op = s->has_no_wrap() ? Bin::MulNoWrap : Bin::Mul;
      break;
    case A::UDiv:
    case A::SDiv:
      op = s->is_exact() ? Bin::DivExact : Bin::Div;
      break;
    case A::URem:
    case A::SRem:
      op = Bin::Rem;
      break;
    case A::UAnd:
    case A::SAnd:
      op = Bin::And;
      break;
    case A::UOr:
    case A::SOr:
      op = Bin::Or;
      break;
    case A::UXor:
    case A::SXor:
      op = Bin::Xor;
      break;
    default:
      return false;
  }
  return true;
}

struct Path {
  ar::BasicBlock* block;
  SymbolicState values;
  std::map< ar::Variable*, Expr > origins;
  std::vector< ar::IntegerType* > parameter_types;
  std::map< SymbolicLoc, Expr > memory;
  std::map< ar::Variable*, ar::Type* > scalar_allocations;
  core::machine_int::ExpressionRanges< int > ranges;
  SymbolicCondition condition = SymbolicCondition::make_true();
  std::map< ar::BasicBlock*, unsigned > visits;

  Path(ar::BasicBlock* b, ar::Function* f)
      : block(b), values(SymbolicState::make_entry(f)) {
    int i = 0;
    for (auto it = f->param_begin(); it != f->param_end(); ++it, ++i) {
      auto* t = ar::dyn_cast< ar::IntegerType >((*it)->type());
      parameter_types.push_back(t);
      if (t)
        origins[*it] = Expr::variable(i, t->bit_width(), t->sign());
    }
  }
  Expr eval(ar::Value* v) const {
    auto* t = ar::dyn_cast< ar::IntegerType >(v->type());
    if (!t)
      return {};
    if (auto* c = ar::dyn_cast< ar::IntegerConstant >(v))
      return Expr::constant(c->value());
    auto value = values.eval(v);
    if (value.is_expr() && value.zero_one && value.expr.constant == 0 &&
        value.expr.coeffs.size() == 1 &&
        value.expr.coeffs.begin()->second == 1) {
      int index = value.expr.coeffs.begin()->first;
      if (index >= 0 &&
          static_cast< std::size_t >(index) < parameter_types.size()) {
        auto* original = parameter_types[static_cast< std::size_t >(index)];
        if (original && original->is_unsigned() && original->bit_width() == 1)
          return Expr::cast(Expr::variable(index, 1, core::Unsigned),
                            t->bit_width(),
                            t->sign());
      }
    }
    if (auto* var = ar::dyn_cast< ar::Variable >(v)) {
      auto it = origins.find(var);
      if (it != origins.end())
        return it->second;
    }
    auto val = values.eval(v);
    if (val.is_expr())
      return condition_expr_from_affine(val.expr, t->bit_width(), t->sign());
    return {};
  }
  Interval interval(const Expr& e) const {
    Interval i = e.evaluate([&](int v, uint64_t w, core::Signedness sign) {
      return ranges.lookup(Expr::variable(v, w, sign));
    });
    i.meet_with(ranges.lookup(e));
    return i;
  }
  // Keep all source comparisons, even if another atom implies them. Only
  // literal constant comparisons and infeasible paths are eliminated.
  bool add(SymbolicConstraint c) {
    if (c.kind == SymbolicConstraint::KUnknown)
      return false;
    if (c.kind == SymbolicConstraint::KTyped) {
      Expr l = c.typed_left, r = c.typed_right;
      if (l.is_constant() && r.is_constant()) {
        if (!compare(c.predicate,
                     l.node().constant.to_z_number(),
                     r.node().constant.to_z_number()))
          condition = SymbolicCondition::make_false();
        return true;
      }
      if (r.is_constant()) {
        Interval current = interval(l);
        if (current.is_bottom()) {
          condition = SymbolicCondition::make_false();
          return true;
        }
        ZNumber lb = current.lb().to_z_number(),
                ub = current.ub().to_z_number();
        ZNumber n = r.node().constant.to_z_number();
        if (equality(c.predicate)) {
          lb = std::max(lb, n);
          ub = std::min(ub, n);
        } else if (inequality(c.predicate)) {
          if (lb == n)
            ++lb;
          if (ub == n)
            --ub;
        } else if (c.predicate == Pred::SIGE || c.predicate == Pred::UIGE)
          lb = std::max(lb, n);
        else if (c.predicate == Pred::SIGT || c.predicate == Pred::UIGT)
          lb = std::max(lb, ZNumber(n + 1));
        else if (c.predicate == Pred::SILE || c.predicate == Pred::UILE)
          ub = std::min(ub, n);
        else if (c.predicate == Pred::SILT || c.predicate == Pred::UILT)
          ub = std::min(ub, ZNumber(n - 1));
        if (lb > ub) {
          condition = SymbolicCondition::make_false();
          return true;
        }
        ranges.record(l,
                      Interval(MachineInt(lb, l.bit_width(), l.sign()),
                               MachineInt(ub, l.bit_width(), l.sign())));
      }
    }
    condition.add_constraint(c);
    return true;
  }
};

SymbolicConstraint constraint(Pred::Predicate p, Expr l, Expr r) {
  return SymbolicConstraint::make_typed(p, std::move(l), std::move(r));
}

bool defined_binary(Path& path,
                    ar::BinaryOperation* s,
                    const Expr& l,
                    const Expr& r,
                    Bin op,
                    bool& complete) {
  if (!l || !r)
    return false;
  if (op == Bin::AddNoWrap || op == Bin::SubNoWrap || op == Bin::MulNoWrap) {
    Interval li = path.interval(l), ri = path.interval(r);
    if (li.is_bottom() || ri.is_bottom()) {
      path.condition = SymbolicCondition::make_false();
      return true;
    }
    auto a = li.to_z_interval(), b = ri.to_z_interval();
    auto result = op == Bin::AddNoWrap   ? a + b
                  : op == Bin::SubNoWrap ? a - b
                                         : a * b;
    ZNumber min = MachineInt::min(l.bit_width(), l.sign()).to_z_number();
    ZNumber max = MachineInt::max(l.bit_width(), l.sign()).to_z_number();
    if (result.lb().is_finite() && result.ub().is_finite() &&
        *result.lb().number() >= min && *result.ub().number() <= max)
      return true;
    // Exact sufficient definedness, using a wider representation that cannot
    // overflow for two operands of the original width. Do not call this full
    // coverage without a proof that all possible return inputs are covered.
    complete = false;
    uint64_t w = l.bit_width() * 2;
    auto wide = Expr::binary(op == Bin::AddNoWrap   ? Bin::Add
                             : op == Bin::SubNoWrap ? Bin::Sub
                                                    : Bin::Mul,
                             Expr::cast(l, w, l.sign()),
                             Expr::cast(r, w, r.sign()));
    auto ge = l.sign() == core::Signed ? Pred::SIGE : Pred::UIGE;
    auto le = l.sign() == core::Signed ? Pred::SILE : Pred::UILE;
    return path.add(constraint(ge,
                               wide,
                               Expr::constant(MachineInt(min, w, l.sign())))) &&
           path.add(constraint(le,
                               wide,
                               Expr::constant(MachineInt(max, w, l.sign()))));
  }
  if (op == Bin::Div || op == Bin::DivExact || op == Bin::Rem) {
    auto ri = path.interval(r);
    if (ri.is_bottom()) {
      path.condition = SymbolicCondition::make_false();
      return true;
    }
    if (ri.lb().to_z_number() <= 0 && ri.ub().to_z_number() >= 0) {
      complete = false;
      if (!path.add(
              constraint(r.sign() == core::Signed ? Pred::SINE : Pred::UINE,
                         r,
                         Expr::constant(
                             MachineInt(0, r.bit_width(), r.sign())))))
        return false;
    }
    // The initial certified division/remainder scope uses positive constants.
    if (!r.is_constant() || r.node().constant.to_z_number() <= 0)
      return false;
    if (op == Bin::DivExact) {
      complete = false;
      return path.add(
          constraint(Pred::SIEQ,
                     Expr::binary(Bin::Rem, l, r),
                     Expr::constant(MachineInt(0, l.bit_width(), l.sign()))));
    }
  }
  return true;
}

ar::Function* callee(ar::CallBase* c) {
  auto* ptr = ar::dyn_cast< ar::FunctionPointerConstant >(c->called());
  return ptr ? ptr->function() : nullptr;
}

bool compose_call(Path& path,
                  ar::CallBase* call,
                  bool normal,
                  const SummaryStore* store,
                  bool& complete) {
  auto* f = callee(call);
  const auto* summary = f && store ? store->get(f) : nullptr;
  if (!summary)
    return false;
  bool available =
      normal ? summary->normal.available : summary->exception.available;
  if (!available)
    return false;
  bool covered =
      normal ? summary->normal.complete : summary->exception.complete;
  const auto& condition =
      normal ? summary->normal.condition : summary->exception.condition;
  std::vector< SymbolicValue > args;
  std::vector< Expr > typed;
  for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
    args.push_back(path.values.eval(*it));
    typed.push_back(path.eval(*it));
  }
  bool exact = true;
  auto translated = subst_condition_into_caller(condition, args, typed, &exact);
  path.condition.meet_with(translated);
  complete = complete && covered && exact;
  // The proof region is explicitly intersected with the callee guarantee, so
  // its conditional effects are usable here, including a Partial guarantee.
  path.values.exec(call);
  invalidate_pointer_arguments(path.values, args);
  path.memory.clear();
  if (!path.condition.is_false())
    apply_memory_summary(path.values,
                         normal ? summary->normal.mu_exit
                                : summary->exception.mu_exit,
                         f,
                         args);
  if (call->has_result())
    path.origins.erase(call->result());
  return true;
}

} // namespace

FunctionSummary prove_exit_conditions(ar::Function* f,
                                      const SummaryStore* store) {
  FunctionSummary result;
  result.function = f;
  result.normal.available = true;
  result.exception.available = true;
  result.normal.complete = true;
  result.exception.complete = true;
  auto* code = f->body();
  result.normal.has_exit = code->normal_exit_block_or_null() != nullptr;
  result.exception.has_exit = code->exception_exit_block_or_null() != nullptr;
  std::vector< Path > pending;
  pending.emplace_back(code->entry_block(), f);
  unsigned budget = 0;
  auto incomplete = [&] {
    result.normal.complete = false;
    result.exception.complete = false;
  };
  while (!pending.empty()) {
    Path path = std::move(pending.back());
    pending.pop_back();
    if (++budget > 1024) {
      incomplete();
      break;
    }
    if (++path.visits[path.block] > 16) {
      incomplete();
      continue;
    }
    bool modeled = true, terminated = false;
    ar::Invoke* invoke = nullptr;
    for (ar::Statement* stmt : *path.block) {
      if (path.condition.is_false())
        break;
      if (stmt->has_undefined_constant_operand()) {
        modeled = false;
        break;
      }
      if (auto* cmp = ar::dyn_cast< ar::Comparison >(stmt)) {
        auto c = subst_constraint(cmp, path.values);
        if (cmp->is_integer_predicate() &&
            c.kind != SymbolicConstraint::KNullity &&
            !(path.values.eval(cmp->left()).is_bool() ||
              path.values.eval(cmp->right()).is_bool()))
          c = constraint(cmp->predicate(),
                         path.eval(cmp->left()),
                         path.eval(cmp->right()));
        if (!path.add(c)) {
          modeled = false;
          break;
        }
      } else if (auto* call = ar::dyn_cast< ar::Call >(stmt)) {
        auto* target = callee(call);
        if (target && target->intrinsic_id() == ar::Intrinsic::LibcAbort) {
          terminated = true;
          break;
        }
        if (!compose_call(path, call, true, store, result.normal.complete)) {
          modeled = false;
          break;
        }
        // Ordinary calls can propagate unmodeled exceptions independently.
        result.exception.complete = false;
        continue;
      } else if (auto* inv = ar::dyn_cast< ar::Invoke >(stmt)) {
        invoke = inv;
        break;
      } else if (auto* assign = ar::dyn_cast< ar::Assignment >(stmt)) {
        path.origins[assign->result()] = path.eval(assign->operand());
      } else if (auto* binary = ar::dyn_cast< ar::BinaryOperation >(stmt)) {
        Bin op;
        auto l = path.eval(binary->left()), r = path.eval(binary->right());
        bool total = true;
        if (!binary_operator(binary, op) ||
            !defined_binary(path, binary, l, r, op, total)) {
          modeled = false;
          break;
        }
        if (!total)
          incomplete();
        path.origins[binary->result()] = Expr::binary(op, l, r);
        if (!path.origins[binary->result()]) {
          modeled = false;
          break;
        }
      } else if (auto* unary = ar::dyn_cast< ar::UnaryOperation >(stmt)) {
        auto* t = ar::dyn_cast< ar::IntegerType >(unary->result()->type());
        auto e = path.eval(unary->operand());
        if (t && e &&
            (unary->op() == ar::UnaryOperation::UTrunc ||
             unary->op() == ar::UnaryOperation::STrunc ||
             unary->op() == ar::UnaryOperation::ZExt ||
             unary->op() == ar::UnaryOperation::SExt ||
             unary->op() == ar::UnaryOperation::Bitcast))
          path.origins[unary->result()] =
              Expr::cast(e, t->bit_width(), t->sign());
        else {
          modeled = false;
          break;
        }
      } else if (auto* store_stmt = ar::dyn_cast< ar::Store >(stmt)) {
        auto addr = path.values.eval(store_stmt->pointer());
        if (!addr.is_loc() || !addr.loc.callee_local ||
            !addr.loc.offset.is_const() || addr.loc.offset.constant != 0 ||
            !path.scalar_allocations.count(addr.loc.base) ||
            path.scalar_allocations[addr.loc.base] !=
                store_stmt->value()->type()) {
          modeled = false;
          break;
        }
        path.memory[addr.loc] = path.eval(store_stmt->value());
      } else if (auto* load = ar::dyn_cast< ar::Load >(stmt)) {
        auto addr = path.values.eval(load->operand());
        if (!addr.is_loc() || !addr.loc.callee_local ||
            !addr.loc.offset.is_const() || addr.loc.offset.constant != 0 ||
            !path.scalar_allocations.count(addr.loc.base) ||
            path.scalar_allocations[addr.loc.base] != load->result()->type()) {
          modeled = false;
          break;
        }
        auto it = path.memory.find(addr.loc);
        if (it == path.memory.end()) {
          modeled = false;
          break;
        }
        path.origins[load->result()] = it->second;
      } else if (auto* alloc = ar::dyn_cast< ar::Allocate >(stmt)) {
        auto* count = ar::dyn_cast< ar::IntegerConstant >(alloc->array_size());
        if (!count || count->value().to_z_number() != 1) {
          modeled = false;
          break;
        }
        // A loop can execute the same allocation statement again. Its AR
        // variable is reused, but the new storage has no initialized value.
        for (auto it = path.memory.begin(); it != path.memory.end();) {
          if (it->first.base == alloc->result())
            it = path.memory.erase(it);
          else
            ++it;
        }
        path.scalar_allocations[alloc->result()] = alloc->allocated_type();
      } else if (ar::isa< ar::Unreachable >(stmt)) {
        terminated = true;
        break;
      } else if (!ar::isa< ar::ReturnValue >(stmt) &&
                 !ar::isa< ar::Resume >(stmt) &&
                 !ar::isa< ar::PointerShift >(stmt)) {
        modeled = false;
        break;
      }
      path.values.exec(stmt);
    }
    if (!modeled) {
      incomplete();
      continue;
    }
    if (terminated || path.condition.is_false())
      continue;
    if (path.block == code->normal_exit_block_or_null()) {
      result.normal.condition.join_with(path.condition);
      continue;
    }
    if (path.block == code->exception_exit_block_or_null()) {
      result.exception.condition.join_with(path.condition);
      continue;
    }
    for (auto it = path.block->successor_begin();
         it != path.block->successor_end();
         ++it) {
      Path next = path;
      next.block = *it;
      if (invoke) {
        bool normal = (*it == invoke->normal_dest());
        bool& coverage =
            normal ? result.normal.complete : result.exception.complete;
        if (!compose_call(next, invoke, normal, store, coverage)) {
          incomplete();
          continue;
        }
        if (!coverage)
          incomplete();
      }
      pending.push_back(std::move(next));
    }
  }
  return result;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
