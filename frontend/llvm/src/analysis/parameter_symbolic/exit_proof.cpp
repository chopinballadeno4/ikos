#include <ikos/frontend/llvm/analysis/parameter_symbolic/exit_proof.hpp>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

#include <ikos/ar/semantic/bundle.hpp>
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
  using ExceptionToken = uint64_t;
  struct ExceptionRecord {
    ExceptionToken token = 0;
    unsigned fields = 0;
  };
  ar::BasicBlock* block;
  std::size_t begin = 0, group = 0;
  struct Frame {
    ar::CallBase* call;
    ar::BasicBlock* block;
    std::size_t begin;
    ar::Variable* object;
    ar::Type* type;
  };
  std::vector< Frame > frames;
  SymbolicState values;
  std::map< ar::Variable*, Expr > origins;
  std::vector< ar::IntegerType* > parameter_types;
  std::map< SymbolicLoc, Expr > memory;
  std::map< ar::Variable*, ar::Type* > scalar_allocations;
  std::map< ar::Variable*, ZNumber > exception_allocations;
  std::map< ar::Variable*, ar::Variable* > exception_addresses;
  std::map< ar::Variable*, ar::Function* > function_addresses;
  std::map< ar::Variable*, ar::Type* > initialized_exceptions;
  std::set< ar::Variable* > rtti_pointers;
  std::map< ar::Variable*, ExceptionRecord > exception_records;
  std::map< ar::Variable*, ExceptionToken > exception_objects,
      exception_selectors;
  ExceptionToken next_exception = 1, pending_exception = 0;
  std::vector< ExceptionToken > caught_exceptions;
  enum Cleanup { SafeCleanup, TerminatingCleanup, UnknownCleanup };
  std::map< ExceptionToken, Cleanup > cleanups;
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
  SymbolicValue value(ar::Value* v) const {
    if (auto* global = ar::dyn_cast< ar::GlobalVariable >(v)) {
      SymbolicLoc location;
      location.base = global;
      location.callee_local = false;
      return SymbolicValue::from_loc(location);
    }
    return values.eval(v);
  }
  void copy_exception_value(ar::Variable* result, ar::Value* operand) {
    auto* v = operand ? ar::dyn_cast< ar::Variable >(operand) : nullptr;
    if (v == result)
      return;
    exception_records.erase(result);
    exception_objects.erase(result);
    exception_selectors.erase(result);
    exception_addresses.erase(result);
    function_addresses.erase(result);
    rtti_pointers.erase(result);
    if (v) {
      if (exception_records.count(v))
        exception_records[result] = exception_records.at(v);
      if (exception_objects.count(v))
        exception_objects[result] = exception_objects.at(v);
      if (exception_selectors.count(v))
        exception_selectors[result] = exception_selectors.at(v);
      if (exception_addresses.count(v))
        exception_addresses[result] = exception_addresses.at(v);
      if (function_addresses.count(v))
        function_addresses[result] = function_addresses.at(v);
      if (rtti_pointers.count(v))
        rtti_pointers.insert(result);
    }
    if (auto* g =
            operand ? ar::dyn_cast< ar::GlobalVariable >(operand) : nullptr)
      if (g->name().compare(0, 4, "_ZTI") == 0)
        rtti_pointers.insert(result);
    if (auto* p = operand ? ar::dyn_cast< ar::FunctionPointerConstant >(operand)
                          : nullptr)
      function_addresses[result] = p->function();
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

// Only transport the two fields of a genuine landing-pad record. Undef is
// allowed as the initial insert-element aggregate, never as a live field.
int exception_field(ar::Type* type, ar::Value* offset) {
  auto* structure = ar::dyn_cast< ar::StructType >(type);
  auto* c = ar::dyn_cast< ar::IntegerConstant >(offset);
  if (!structure || !c || structure->layout().size() != 2)
    return -1;
  const auto& fields = structure->layout();
  if (!fields[0].type->is_pointer() || !fields[1].type->is_integer())
    return -1;
  for (int i = 0; i != 2; ++i)
    if (fields[static_cast< std::size_t >(i)].offset ==
        c->value().to_z_number())
      return i;
  return -1;
}

enum class RuntimeFlow {
  NotRuntime,
  Unsupported,
  Normal,
  Exception,
  Terminate
};

struct MemberName {
  std::string context, name;
  bool constructor = false, destructor = false;
};

MemberName member_name(ar::Function* function) {
  MemberName result;
  llvm::ItaniumPartialDemangler demangler;
  if (!function || demangler.partialDemangle(function->name().c_str()) ||
      !demangler.isCtorOrDtor())
    return result;
  auto read = [](char* value) {
    std::string text = value ? value : "";
    std::free(value);
    return text;
  };
  result.context = read(demangler.getFunctionDeclContextName(nullptr, nullptr));
  result.name = read(demangler.getFunctionBaseName(nullptr, nullptr));
  result.destructor = !result.name.empty() && result.name[0] == '~';
  result.constructor = !result.destructor && !result.name.empty();
  return result;
}

bool standard_exception(const MemberName& member) {
  static const std::set< std::string > names = {"std::logic_error",
                                                "std::domain_error",
                                                "std::invalid_argument",
                                                "std::length_error",
                                                "std::out_of_range",
                                                "std::runtime_error",
                                                "std::range_error",
                                                "std::overflow_error",
                                                "std::underflow_error"};
  return names.count(member.context) != 0;
}

bool immutable_c_string(const Path& path, ar::Value* value) {
  auto pointer = path.value(value);
  if (!pointer.is_loc() || !pointer.loc.offset.is_const() ||
      pointer.loc.offset.constant < 0)
    return false;
  auto* global = ar::dyn_cast< ar::GlobalVariable >(pointer.loc.base);
  if (!global || !global->has_frontend())
    return false;
  auto* source = global->frontend< llvm::GlobalVariable >();
  if (!source->isConstant() || !source->hasInitializer())
    return false;
  auto* bytes =
      llvm::dyn_cast< llvm::ConstantDataArray >(source->getInitializer());
  if (!bytes || !bytes->isString())
    return false;
  auto string = bytes->getAsString();
  ZNumber offset(pointer.loc.offset.constant);
  if (offset >= ZNumber(string.size()))
    return false;
  return string.drop_front(offset.to< std::size_t >()).contains('\0');
}

bool returns_void_or_this(ar::Function* function) {
  if (function->type()->return_type()->is_void())
    return true;
  // Darwin's constructor/destructor ABI returns this. LLVM's returned
  // attribute establishes that identity even if debug types introduce casts.
  return function->type()->return_type()->is_pointer() &&
         function->has_frontend() &&
         function->frontend< llvm::Function >()
             ->hasParamAttribute(0, llvm::Attribute::Returned);
}

bool standard_constructor_model(ar::Function* function,
                                const Path& path,
                                ar::CallBase* call) {
  auto member = member_name(function);
  if (!function || !function->is_declaration() || !member.constructor ||
      !standard_exception(member) || call->num_arguments() != 2 ||
      function->num_parameters() != 2 || function->type()->is_var_arg() ||
      !immutable_c_string(path, call->argument(1)))
    return false;
  auto* message =
      ar::dyn_cast< ar::PointerType >(function->type()->param_type(1));
  auto* character =
      message ? ar::dyn_cast< ar::IntegerType >(message->pointee()) : nullptr;
  if (!character || character->bit_width() != 8)
    return false;
  auto* self = ar::dyn_cast< ar::PointerType >(function->type()->param_type(0));
  if (!self || !self->pointee()->is_struct() || !returns_void_or_this(function))
    return false;
  // The built-in allocation contract is invalidated by visible replacement
  // allocation functions or installed new-handlers in this module.
  if (function->has_frontend()) {
    auto* source = function->frontend< llvm::Function >();
    for (const auto& f : *source->getParent()) {
      auto name = f.getName();
      if ((name.startswith("_Zn") && !f.isDeclaration()) ||
          (name.contains("set_new_handler") && !f.use_empty()))
        return false;
    }
  }
  return true;
}

ar::Variable* construction_object(const Path& path, ar::CallBase* call) {
  auto* function = callee(call);
  if (!function || !member_name(function).constructor ||
      call->num_arguments() == 0 || !returns_void_or_this(function) ||
      function->type()->is_var_arg())
    return nullptr;
  auto* address = ar::dyn_cast< ar::Variable >(call->argument(0));
  auto pointer = path.values.eval(call->argument(0));
  auto* type = ar::dyn_cast< ar::PointerType >(call->argument(0)->type());
  if (!address || !type || !type->pointee()->is_struct() || !pointer.is_loc() ||
      !pointer.loc.offset.is_const() || pointer.loc.offset.constant != 0 ||
      !path.exception_addresses.count(address) ||
      path.exception_addresses.at(address) != pointer.loc.base ||
      !path.exception_allocations.count(pointer.loc.base) ||
      function->bundle()->data_layout().alloc_size_in_bytes(type->pointee()) >
          path.exception_allocations.at(pointer.loc.base))
    return nullptr;
  return pointer.loc.base;
}

Path::Cleanup destructor_cleanup(ar::Function* function,
                                 const SummaryStore* store) {
  if (!function || function->num_parameters() != 1 ||
      !returns_void_or_this(function))
    return Path::UnknownCleanup;
  auto* self = ar::dyn_cast< ar::PointerType >(function->type()->param_type(0));
  if (!self || !self->pointee()->is_struct())
    return Path::UnknownCleanup;
  auto member = member_name(function);
  if (!member.destructor)
    return Path::UnknownCleanup;
  if (function->is_declaration())
    return standard_exception(member) ? Path::SafeCleanup
                                      : Path::UnknownCleanup;
  static thread_local std::set< ar::Function* > active;
  if (!active.insert(function).second)
    return Path::UnknownCleanup;
  auto summary = prove_exit_conditions(function, store);
  active.erase(function);
  if (!summary.memory_preconditions.empty() || !summary.normal.complete ||
      !summary.exception.complete || !summary.exception.condition.is_false())
    return Path::UnknownCleanup;
  if (summary.normal.condition.is_true())
    return Path::SafeCleanup;
  if (summary.normal.condition.is_false())
    return Path::TerminatingCleanup;
  return Path::UnknownCleanup;
}

RuntimeFlow runtime_call(Path& path,
                         ar::CallBase* call,
                         const SummaryStore* store) {
  auto* f = callee(call);
  if (!f)
    return RuntimeFlow::NotRuntime;
  switch (f->intrinsic_id()) {
    case ar::Intrinsic::LibcAbort:
      return RuntimeFlow::Terminate;
    case ar::Intrinsic::LibcppAllocateException: {
      auto* size = ar::dyn_cast< ar::IntegerConstant >(call->argument(0));
      if (!call->has_result() || !size || size->value().to_z_number() <= 0)
        return RuntimeFlow::Unsupported;
      path.values.exec(call);
      path.origins.erase(call->result());
      path.copy_exception_value(call->result(), nullptr);
      auto* base = call->result();
      // Re-executing an allocation creates different storage. Do not let an
      // alias of its previous execution name the fresh exception object.
      for (auto& kv : path.values.sigma)
        if (kv.second.is_loc() && kv.second.loc.base == base)
          kv.second = SymbolicValue::top();
      for (auto& kv : path.values.mu)
        if (kv.first.base == base ||
            (kv.second.is_loc() && kv.second.loc.base == base))
          kv.second = SymbolicValue::top();
      for (auto it = path.exception_addresses.begin();
           it != path.exception_addresses.end();)
        if (it->second == base)
          it = path.exception_addresses.erase(it);
        else
          ++it;
      path.exception_addresses[base] = base;
      path.exception_allocations[base] = size->value().to_z_number();
      path.initialized_exceptions.erase(base);
      for (auto it = path.memory.begin(); it != path.memory.end();)
        if (it->first.base == base)
          it = path.memory.erase(it);
        else
          ++it;
      SymbolicLoc loc;
      loc.base = base;
      loc.callee_local = true;
      path.values.local_allocs.insert(base);
      path.values.sigma[base] = SymbolicValue::from_loc(loc);
      return RuntimeFlow::Normal;
    }
    case ar::Intrinsic::LibcppThrow: {
      auto object = path.values.eval(call->argument(0));
      auto* address = ar::dyn_cast< ar::Variable >(call->argument(0));
      auto* rtti = ar::dyn_cast< ar::Variable >(call->argument(1));
      auto* global = ar::dyn_cast< ar::GlobalVariable >(call->argument(1));
      bool known_rtti = (global && global->name().compare(0, 4, "_ZTI") == 0) ||
                        (rtti && path.rtti_pointers.count(rtti));
      if (!object.is_loc() || !object.loc.offset.is_const() ||
          object.loc.offset.constant != 0 || !address ||
          !path.exception_addresses.count(address) ||
          path.exception_addresses.at(address) != object.loc.base ||
          !path.exception_allocations.count(object.loc.base) ||
          !path.initialized_exceptions.count(object.loc.base) || !known_rtti)
        return RuntimeFlow::Unsupported;
      Path::Cleanup cleanup = Path::SafeCleanup;
      if (!path.values.eval(call->argument(2)).is_null()) {
        ar::Function* destructor = nullptr;
        if (auto* pointer =
                ar::dyn_cast< ar::FunctionPointerConstant >(call->argument(2)))
          destructor = pointer->function();
        else if (auto* variable =
                     ar::dyn_cast< ar::Variable >(call->argument(2))) {
          auto it = path.function_addresses.find(variable);
          if (it != path.function_addresses.end())
            destructor = it->second;
        }
        if (!destructor)
          return RuntimeFlow::Unsupported;
        cleanup = destructor_cleanup(destructor, store);
      }
      // Ownership passes to the exception runtime. A later throw/free of the
      // raw pointer is not a fresh exception; rethrow uses the active token.
      path.initialized_exceptions.erase(object.loc.base);
      path.exception_allocations.erase(object.loc.base);
      path.pending_exception = path.next_exception++;
      path.cleanups[path.pending_exception] = cleanup;
      return RuntimeFlow::Exception;
    }
    case ar::Intrinsic::LibcppRethrow:
      if (path.caught_exceptions.empty())
        return RuntimeFlow::Unsupported;
      path.pending_exception = path.caught_exceptions.back();
      return RuntimeFlow::Exception;
    case ar::Intrinsic::LibcppBeginCatch: {
      auto* arg = ar::dyn_cast< ar::Variable >(call->argument(0));
      if (!arg || !path.pending_exception ||
          !path.exception_objects.count(arg) ||
          path.exception_objects.at(arg) != path.pending_exception)
        return RuntimeFlow::Unsupported;
      path.caught_exceptions.push_back(path.pending_exception);
      path.pending_exception = 0;
      path.values.exec(call);
      if (call->has_result()) {
        path.origins.erase(call->result());
        path.copy_exception_value(call->result(), arg);
      }
      return RuntimeFlow::Normal;
    }
    case ar::Intrinsic::LibcppEndCatch:
      if (path.caught_exceptions.empty())
        return RuntimeFlow::Unsupported;
      if (path.pending_exception != path.caught_exceptions.back()) {
        auto cleanup = path.cleanups.find(path.caught_exceptions.back());
        if (cleanup == path.cleanups.end() ||
            cleanup->second == Path::UnknownCleanup)
          return RuntimeFlow::Unsupported;
        if (cleanup->second == Path::TerminatingCleanup)
          return RuntimeFlow::Terminate;
      }
      path.caught_exceptions.pop_back();
      return RuntimeFlow::Normal;
    case ar::Intrinsic::LibcppFreeException: {
      auto object = path.values.eval(call->argument(0));
      auto* address = ar::dyn_cast< ar::Variable >(call->argument(0));
      if (!object.is_loc() || !object.loc.offset.is_const() ||
          object.loc.offset.constant != 0 || !address ||
          !path.exception_addresses.count(address) ||
          path.exception_addresses.at(address) != object.loc.base ||
          !path.exception_allocations.count(object.loc.base))
        return RuntimeFlow::Unsupported;
      path.exception_allocations.erase(object.loc.base);
      path.initialized_exceptions.erase(object.loc.base);
      return RuntimeFlow::Normal;
    }
    default:
      return RuntimeFlow::NotRuntime;
  }
}

// Translate a writable formal into another entry requirement, or discharge it
// using an allocation owned by this proof path. External loads remain outside
// this scope: a write does not establish arbitrary alias/read relationships.
bool require_writable(Path& path,
                      const SymbolicValue& pointer,
                      ar::IntegerType* type,
                      uint64_t alignment,
                      std::vector< WritableMemory >& requirements) {
  if (!pointer.is_loc() || !pointer.loc.base || !type ||
      type->bit_width() == 0 || type->bit_width() % 8 != 0 ||
      !pointer.loc.offset.is_const() || pointer.loc.offset.constant != 0)
    return false;
  auto* pointer_type =
      ar::dyn_cast< ar::PointerType >(pointer.loc.base->type());
  if (!pointer_type || pointer_type->pointee() != type)
    return false;
  alignment = std::max(uint64_t(1), alignment);
  if (pointer.loc.callee_local) {
    auto* local = ar::dyn_cast< ar::LocalVariable >(pointer.loc.base);
    return local && path.scalar_allocations.count(local) &&
           path.scalar_allocations.at(local) == type &&
           local->alignment() >= alignment &&
           local->alignment() % alignment == 0;
  }
  int parameter = pointer_parameter_index(pointer.loc);
  if (parameter < 0)
    return false;
  WritableMemory requirement{parameter, type, alignment};
  if (std::find(requirements.begin(), requirements.end(), requirement) ==
      requirements.end())
    requirements.push_back(requirement);
  return true;
}

bool compose_call(Path& path,
                  ar::CallBase* call,
                  bool normal,
                  const SummaryStore* store,
                  bool& complete,
                  std::vector< WritableMemory >& requirements) {
  auto* f = callee(call);
  const auto* summary = f && store ? store->get(f) : nullptr;
  FunctionSummary destructor_summary;
  if (f && f->is_definition() && member_name(f).destructor &&
      (!summary || !summary->normal.complete || !summary->exception.complete)) {
    // ABI destructor wrappers (D1 -> D2) need the same body proof even when
    // native exception handling has not visited their definitions yet.
    static thread_local std::set< ar::Function* > active;
    if (active.size() >= 16 || !active.insert(f).second)
      return false;
    destructor_summary = prove_exit_conditions(f, store);
    active.erase(f);
    summary = &destructor_summary;
  }
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
  for (const auto& requirement : summary->memory_preconditions) {
    if (requirement.parameter < 0 ||
        static_cast< std::size_t >(requirement.parameter) >= args.size() ||
        !require_writable(path,
                          args[static_cast< std::size_t >(
                              requirement.parameter)],
                          requirement.type,
                          requirement.alignment,
                          requirements))
      return false;
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
  if (call->has_result()) {
    path.origins.erase(call->result());
    path.copy_exception_value(call->result(), nullptr);
  }
  if (!normal && !path.condition.is_false()) {
    path.pending_exception = path.next_exception++;
    path.cleanups[path.pending_exception] = summary->exception.cleanup_safe
                                                ? Path::SafeCleanup
                                                : Path::UnknownCleanup;
  }
  return true;
}

// Each modeled nondeterministic operation creates two child obligations. A
// guarantee is their intersection, never the union of favorable outcomes.
struct ProofGroup {
  SymbolicCondition normal = SymbolicCondition::make_false();
  SymbolicCondition exception = SymbolicCondition::make_false();
  std::vector< std::pair< std::size_t, std::size_t > > alternatives;
};

bool enter_constructor(Path& path,
                       ar::CallBase* call,
                       std::size_t next,
                       ar::Variable* object) {
  auto* function = callee(call);
  if (!function || !function->is_definition() || path.frames.size() >= 16 ||
      function->num_parameters() != call->num_arguments() ||
      function == path.block->code()->function())
    return false;
  for (const auto& frame : path.frames)
    if (frame.block->code()->function() == function)
      return false;
  std::vector< SymbolicValue > values;
  std::vector< Expr > origins;
  for (auto it = call->arg_begin(); it != call->arg_end(); ++it) {
    values.push_back(path.value(*it));
    origins.push_back(path.eval(*it));
  }
  auto* type =
      ar::cast< ar::PointerType >(call->argument(0)->type())->pointee();
  auto* invoke = ar::dyn_cast< ar::Invoke >(call);
  path.frames.push_back({call,
                         invoke ? invoke->normal_dest() : path.block,
                         invoke ? 0 : next,
                         object,
                         type});
  for (std::size_t i = 0; i < values.size(); ++i) {
    auto* formal = function->param(i);
    path.copy_exception_value(formal,
                              call->argument(static_cast< unsigned >(i)));
    path.values.sigma[formal] = values[i];
    path.origins[formal] = origins[i];
  }
  path.block = function->body()->entry_block();
  path.begin = 0;
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
  std::vector< ProofGroup > groups(1);
  bool cleanup_safe = true;
  auto collect_exception = [&](const Path& path) {
    groups[path.group].exception.join_with(path.condition);
    result.exception.has_exit = true;
    auto cleanup = path.cleanups.find(path.pending_exception);
    if (cleanup == path.cleanups.end() || cleanup->second != Path::SafeCleanup)
      cleanup_safe = false;
  };
  auto propagate_exception = [&](Path path) {
    while (!path.frames.empty()) {
      auto frame = path.frames.back();
      path.frames.pop_back();
      path.initialized_exceptions.erase(frame.object);
      if (auto* invoke = ar::dyn_cast< ar::Invoke >(frame.call)) {
        path.block = invoke->exception_dest();
        path.begin = 0;
        pending.push_back(std::move(path));
        return;
      }
    }
    collect_exception(path);
  };
  auto split = [&](std::size_t parent) {
    std::size_t normal = groups.size(), exception = normal + 1;
    groups.resize(groups.size() + 2);
    groups[parent].alternatives.emplace_back(normal, exception);
    return std::make_pair(normal, exception);
  };
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
    std::size_t index = 0;
    for (ar::Statement* stmt : *path.block) {
      if (index++ < path.begin)
        continue;
      if (path.condition.is_false())
        break;
      if (stmt->has_undefined_constant_operand() &&
          !ar::isa< ar::InsertElement >(stmt)) {
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
        if (auto* object = construction_object(path, call)) {
          if (callee(call)->is_definition()) {
            if (!enter_constructor(path, call, index, object)) {
              modeled = false;
              break;
            }
            pending.push_back(std::move(path));
            terminated = true;
            break;
          }
          if (standard_constructor_model(callee(call), path, call)) {
            auto children = split(path.group);
            Path escaping = path;
            escaping.group = children.second;
            escaping.pending_exception = escaping.next_exception++;
            escaping.cleanups[escaping.pending_exception] = Path::SafeCleanup;
            propagate_exception(std::move(escaping));
            path.group = children.first;
            path.initialized_exceptions[object] =
                ar::cast< ar::PointerType >(call->argument(0)->type())
                    ->pointee();
            if (call->has_result()) {
              path.copy_exception_value(call->result(), call->argument(0));
              path.values.sigma[call->result()] =
                  path.values.eval(call->argument(0));
            }
            continue;
          }
        }
        auto flow = runtime_call(path, call, store);
        if (flow == RuntimeFlow::Exception) {
          propagate_exception(path);
          terminated = true;
          break;
        }
        if (flow == RuntimeFlow::Terminate) {
          terminated = true;
          break;
        }
        if (flow == RuntimeFlow::Normal)
          continue;
        if (flow == RuntimeFlow::Unsupported) {
          modeled = false;
          break;
        }
        // An ordinary call has no local handler: certified callee exception
        // paths propagate out of this function; only normal paths continue.
        Path escaping = path;
        bool normal_complete = true, exception_complete = true;
        bool exception_modeled = compose_call(escaping,
                                              call,
                                              false,
                                              store,
                                              exception_complete,
                                              result.memory_preconditions);
        if (exception_modeled && !escaping.condition.is_false()) {
          propagate_exception(std::move(escaping));
        }
        if (!compose_call(path,
                          call,
                          true,
                          store,
                          normal_complete,
                          result.memory_preconditions)) {
          modeled = false;
          break;
        }
        if (!normal_complete || !exception_modeled || !exception_complete)
          incomplete();
        continue;
      } else if (auto* inv = ar::dyn_cast< ar::Invoke >(stmt)) {
        if (auto* object = construction_object(path, inv)) {
          if (callee(inv)->is_definition()) {
            if (!enter_constructor(path, inv, 0, object)) {
              modeled = false;
              break;
            }
            pending.push_back(std::move(path));
            terminated = true;
            break;
          }
          if (standard_constructor_model(callee(inv), path, inv)) {
            auto children = split(path.group);
            Path escaping = path;
            escaping.group = children.second;
            escaping.block = inv->exception_dest();
            escaping.begin = 0;
            escaping.pending_exception = escaping.next_exception++;
            escaping.cleanups[escaping.pending_exception] = Path::SafeCleanup;
            pending.push_back(std::move(escaping));
            path.group = children.first;
            path.initialized_exceptions[object] =
                ar::cast< ar::PointerType >(inv->argument(0)->type())
                    ->pointee();
            if (inv->has_result()) {
              path.copy_exception_value(inv->result(), inv->argument(0));
              path.values.sigma[inv->result()] =
                  path.values.eval(inv->argument(0));
            }
            path.block = inv->normal_dest();
            path.begin = 0;
            pending.push_back(std::move(path));
            terminated = true;
            break;
          }
        }
        invoke = inv;
        break;
      } else if (auto* assign = ar::dyn_cast< ar::Assignment >(stmt)) {
        path.origins[assign->result()] = path.eval(assign->operand());
        path.copy_exception_value(assign->result(), assign->operand());
        if (ar::isa< ar::GlobalVariable >(assign->operand())) {
          path.values.sigma[assign->result()] = path.value(assign->operand());
          continue;
        }
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
        if (unary->op() == ar::UnaryOperation::Bitcast &&
            unary->result()->type()->is_pointer() &&
            unary->operand()->type()->is_pointer()) {
          path.copy_exception_value(unary->result(), unary->operand());
          if (ar::isa< ar::GlobalVariable >(unary->operand())) {
            path.values.sigma[unary->result()] = path.value(unary->operand());
            continue;
          }
        } else if (t && e &&
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
        bool scalar = addr.is_loc() && addr.loc.callee_local &&
                      addr.loc.offset.is_const() &&
                      addr.loc.offset.constant == 0 &&
                      path.scalar_allocations.count(addr.loc.base) &&
                      path.scalar_allocations[addr.loc.base] ==
                          store_stmt->value()->type();
        auto* integer =
            ar::dyn_cast< ar::IntegerType >(store_stmt->value()->type());
        auto* address = ar::dyn_cast< ar::Variable >(store_stmt->pointer());
        bool exception_object =
            addr.is_loc() && addr.loc.offset.is_const() &&
            addr.loc.offset.constant == 0 && integer && address &&
            path.exception_addresses.count(address) &&
            path.exception_addresses.at(address) == addr.loc.base &&
            path.exception_allocations.count(addr.loc.base) &&
            path.exception_allocations.at(addr.loc.base) ==
                ZNumber((integer->bit_width() + 7) / 8) &&
            bool(path.eval(store_stmt->value()));
        bool external = !store_stmt->is_volatile() && addr.is_loc() &&
                        !addr.loc.callee_local && integer &&
                        bool(path.eval(store_stmt->value())) &&
                        require_writable(path,
                                         addr,
                                         integer,
                                         store_stmt->alignment(),
                                         result.memory_preconditions);
        bool class_field = false;
        if (addr.is_loc() && address &&
            path.exception_addresses.count(address) &&
            path.exception_addresses.at(address) == addr.loc.base &&
            addr.loc.offset.is_const() && addr.loc.offset.constant >= 0 &&
            path.exception_allocations.count(addr.loc.base)) {
          auto& layout = f->bundle()->data_layout();
          auto bytes = layout.store_size_in_bytes(store_stmt->value()->type());
          uint64_t alignment = std::max(uint64_t(1), store_stmt->alignment());
          auto value = path.value(store_stmt->value());
          bool defined = integer ? bool(path.eval(store_stmt->value()))
                                 : (value.is_loc() || value.is_null() ||
                                    ar::isa< ar::FunctionPointerConstant >(
                                        store_stmt->value()));
          class_field =
              defined &&
              ZNumber(addr.loc.offset.constant) + bytes <=
                  path.exception_allocations.at(addr.loc.base) &&
              ZNumber(addr.loc.offset.constant) % ZNumber(alignment) == 0;
        }
        if (store_stmt->is_volatile() ||
            (!scalar && !exception_object && !external && !class_field)) {
          modeled = false;
          break;
        }
        auto stored = path.eval(store_stmt->value());
        for (auto it = path.memory.begin(); it != path.memory.end();) {
          bool disjoint = false;
          if (it->first.base == addr.loc.base && stored && it->second &&
              it->first.offset.is_const() && addr.loc.offset.is_const()) {
            ZNumber old_begin(it->first.offset.constant),
                new_begin(addr.loc.offset.constant);
            disjoint =
                old_begin + ZNumber((it->second.bit_width() + 7) / 8) <=
                    new_begin ||
                new_begin + ZNumber((stored.bit_width() + 7) / 8) <= old_begin;
          }
          if (it->first.base == addr.loc.base && !disjoint)
            it = path.memory.erase(it);
          else
            ++it;
        }
        path.memory[addr.loc] = stored;
        if (exception_object)
          path.initialized_exceptions[addr.loc.base] =
              store_stmt->value()->type();
      } else if (auto* load = ar::dyn_cast< ar::Load >(stmt)) {
        auto addr = path.values.eval(load->operand());
        bool scalar =
            addr.is_loc() && addr.loc.callee_local &&
            addr.loc.offset.is_const() && addr.loc.offset.constant == 0 &&
            path.scalar_allocations.count(addr.loc.base) &&
            path.scalar_allocations[addr.loc.base] == load->result()->type();
        auto* address = ar::dyn_cast< ar::Variable >(load->operand());
        bool class_field =
            addr.is_loc() && address &&
            path.exception_addresses.count(address) &&
            path.exception_addresses.at(address) == addr.loc.base &&
            path.exception_allocations.count(addr.loc.base) &&
            addr.loc.offset.is_const() && addr.loc.offset.constant >= 0 &&
            ZNumber(addr.loc.offset.constant) +
                    f->bundle()->data_layout().store_size_in_bytes(
                        load->result()->type()) <=
                path.exception_allocations.at(addr.loc.base);
        if (load->is_volatile() || (!scalar && !class_field)) {
          modeled = false;
          break;
        }
        auto it = path.memory.find(addr.loc);
        auto* type = ar::dyn_cast< ar::IntegerType >(load->result()->type());
        if (it == path.memory.end() || !it->second || !type ||
            it->second.bit_width() != type->bit_width() ||
            it->second.sign() != type->sign()) {
          modeled = false;
          break;
        }
        path.origins[load->result()] = it->second;
        path.copy_exception_value(load->result(), nullptr);
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
      } else if (auto* landing = ar::dyn_cast< ar::LandingPad >(stmt)) {
        if (!path.pending_exception) {
          modeled = false;
          break;
        }
        path.exception_records[landing->result()] = {path.pending_exception, 3};
      } else if (auto* extract = ar::dyn_cast< ar::ExtractElement >(stmt)) {
        auto* aggregate = ar::dyn_cast< ar::Variable >(extract->aggregate());
        int field =
            exception_field(extract->aggregate()->type(), extract->offset());
        if (!aggregate || field < 0 ||
            !path.exception_records.count(aggregate) ||
            !(path.exception_records.at(aggregate).fields & (1u << field))) {
          modeled = false;
          break;
        }
        auto token = path.exception_records.at(aggregate).token;
        path.origins[extract->result()] = {};
        path.copy_exception_value(extract->result(), nullptr);
        if (field == 0)
          path.exception_objects[extract->result()] = token;
        else
          path.exception_selectors[extract->result()] = token;
      } else if (auto* insert = ar::dyn_cast< ar::InsertElement >(stmt)) {
        int field = exception_field(insert->result()->type(), insert->offset());
        auto* value = ar::dyn_cast< ar::Variable >(insert->element());
        Path::ExceptionRecord record;
        if (auto* aggregate =
                ar::dyn_cast< ar::Variable >(insert->aggregate())) {
          if (!path.exception_records.count(aggregate)) {
            modeled = false;
            break;
          }
          record = path.exception_records.at(aggregate);
        } else if (!ar::isa< ar::UndefinedConstant >(insert->aggregate())) {
          modeled = false;
          break;
        }
        auto& tokens =
            field == 0 ? path.exception_objects : path.exception_selectors;
        if (field < 0 || !value || !tokens.count(value) ||
            (record.token && record.token != tokens.at(value))) {
          modeled = false;
          break;
        }
        record.token = tokens.at(value);
        record.fields |= (1u << field);
        path.exception_records[insert->result()] = record;
      } else if (auto* resume = ar::dyn_cast< ar::Resume >(stmt)) {
        auto it = path.exception_records.find(resume->operand());
        if (!path.pending_exception || it == path.exception_records.end() ||
            it->second.fields != 3 ||
            it->second.token != path.pending_exception) {
          modeled = false;
          break;
        }
        propagate_exception(path);
        terminated = true;
        break;
      } else if (ar::isa< ar::Unreachable >(stmt)) {
        terminated = true;
        break;
      } else if (auto* ret = ar::dyn_cast< ar::ReturnValue >(stmt)) {
        if (!path.frames.empty()) {
          auto frame = path.frames.back();
          path.frames.pop_back();
          if (!path.exception_allocations.count(frame.object)) {
            modeled = false;
            break;
          }
          if (frame.call->has_result()) {
            if (!ret->has_operand()) {
              modeled = false;
              break;
            }
            auto value = path.values.eval(ret->operand());
            auto origin = path.eval(ret->operand());
            path.copy_exception_value(frame.call->result(), ret->operand());
            path.values.sigma[frame.call->result()] = value;
            path.origins[frame.call->result()] = origin;
          }
          path.initialized_exceptions[frame.object] = frame.type;
          path.block = frame.block;
          path.begin = frame.begin;
          pending.push_back(std::move(path));
          terminated = true;
          break;
        }
      } else if (auto* shift = ar::dyn_cast< ar::PointerShift >(stmt)) {
        auto pointer = path.value(shift->pointer());
        auto* source = ar::dyn_cast< ar::Variable >(shift->pointer());
        ar::Variable* base = source && path.exception_addresses.count(source)
                                 ? path.exception_addresses.at(source)
                                 : nullptr;
        path.values.exec(stmt);
        if (pointer.is_loc() && pointer.loc.offset.is_const()) {
          ZNumber offset(pointer.loc.offset.constant);
          bool constant = true;
          for (auto it = shift->term_begin(); it != shift->term_end(); ++it) {
            auto element = *it;
            auto term = path.eval(element.second);
            if (!term || !term.is_constant()) {
              constant = false;
              break;
            }
            offset += element.first.to_z_number() *
                      term.node().constant.to_z_number();
          }
          if (constant && offset.fits< int >()) {
            pointer.loc.offset = SymbolicExpr::from_const(offset.to< int >());
            path.values.sigma[shift->result()] = pointer;
          }
        }
        path.copy_exception_value(shift->result(), nullptr);
        auto value = path.values.eval(shift->result());
        if (base && value.is_loc() && value.loc.base == base &&
            value.loc.offset.is_const() && value.loc.offset.constant >= 0 &&
            path.exception_allocations.count(base) &&
            ZNumber(value.loc.offset.constant) <=
                path.exception_allocations.at(base))
          path.exception_addresses[shift->result()] = base;
        continue;
      } else if (!ar::isa< ar::ReturnValue >(stmt) &&
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
      groups[path.group].normal.join_with(path.condition);
      continue;
    }
    if (path.block == code->exception_exit_block_or_null()) {
      propagate_exception(path);
      continue;
    }
    for (auto it = path.block->successor_begin();
         it != path.block->successor_end();
         ++it) {
      Path next = path;
      next.block = *it;
      next.begin = 0;
      if (invoke) {
        bool normal = (*it == invoke->normal_dest());
        auto flow = runtime_call(next, invoke, store);
        if (flow == RuntimeFlow::Terminate)
          continue;
        if (flow == RuntimeFlow::Normal || flow == RuntimeFlow::Exception) {
          if (normal != (flow == RuntimeFlow::Normal))
            continue;
        } else if (flow == RuntimeFlow::Unsupported) {
          incomplete();
          continue;
        } else {
          bool coverage = true;
          if (!compose_call(next,
                            invoke,
                            normal,
                            store,
                            coverage,
                            result.memory_preconditions)) {
            incomplete();
            continue;
          }
          if (!coverage)
            incomplete();
        }
      }
      pending.push_back(std::move(next));
    }
  }
  for (std::size_t i = groups.size(); i-- > 0;) {
    auto common = [](SymbolicCondition left,
                     const SymbolicCondition& right,
                     bool& complete) {
      if (!right.disjuncts.empty() &&
          left.disjuncts.size() > 1024 / right.disjuncts.size()) {
        complete = false;
        return SymbolicCondition::make_false();
      }
      left.meet_with(right);
      return left;
    };
    for (auto children : groups[i].alternatives) {
      // Mixed normal/exception/termination outcomes do not establish exact
      // per-exit coverage, even when their common guarantee is empty.
      if (!groups[children.first].normal.equals(groups[children.second].normal))
        result.normal.complete = false;
      if (!groups[children.first].exception.equals(
              groups[children.second].exception))
        result.exception.complete = false;
      auto normal = common(groups[children.first].normal,
                           groups[children.second].normal,
                           result.normal.complete);
      groups[i].normal.join_with(normal);
      auto exception = common(groups[children.first].exception,
                              groups[children.second].exception,
                              result.exception.complete);
      groups[i].exception.join_with(exception);
    }
  }
  result.normal.condition = std::move(groups[0].normal);
  result.exception.condition = std::move(groups[0].exception);
  result.exception.cleanup_safe = result.exception.complete && cleanup_safe;
  return result;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
