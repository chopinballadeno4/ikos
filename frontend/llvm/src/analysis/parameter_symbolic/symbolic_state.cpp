#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_state.hpp>

#include <ikos/ar/semantic/bundle.hpp>
#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>
#include <ikos/ar/semantic/type.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// make_entry
// ---------------------------------------------------------------------------

SymbolicState SymbolicState::make_entry(ar::Function* fun) {
  SymbolicState s;
  int idx = 0;
  for (auto it = fun->param_begin(); it != fun->param_end(); ++it, ++idx) {
    ar::InternalVariable* p = *it;
    ar::Type* ty = p->type();
    std::string name = p->has_name() ? p->name() : ("p" + std::to_string(idx));
    s.param_names.push_back(name);

    if (ar::isa< ar::IntegerType >(ty) || ar::isa< ar::FloatType >(ty)) {
      auto* integer = ar::dyn_cast< ar::IntegerType >(ty);
      s.sigma[p] = SymbolicValue::from_expr(SymbolicExpr::from_param(idx),
                                            integer && integer->is_unsigned() &&
                                                integer->bit_width() == 1);
    } else if (ar::isa< ar::PointerType >(ty)) {
      SymbolicLoc loc;
      loc.base = p;
      loc.callee_local = false;
      s.sigma[p] = SymbolicValue::from_loc(loc);
    } else {
      s.sigma[p] = SymbolicValue::top();
    }
  }
  return s;
}

// ---------------------------------------------------------------------------
// join_with (in-place pointwise merge)
// ---------------------------------------------------------------------------

template < typename Map >
static void merge_map(Map& dst, const Map& src) {
  // Missing entries denote TOP. Therefore, an entry present on only one
  // reachable predecessor is merged with TOP, not copied to the result.
  for (auto it2 = src.begin(); it2 != src.end(); ++it2) {
    const typename Map::key_type& key = it2->first;
    const SymbolicValue& val = it2->second;
    auto it = dst.find(key);
    if (it == dst.end()) {
      dst[key] = SymbolicValue::top();
    } else {
      it->second = merge_val(it->second, val);
    }
  }

  for (auto it = dst.begin(); it != dst.end(); ++it) {
    if (src.find(it->first) == src.end()) {
      it->second = SymbolicValue::top();
    }
  }
}

void SymbolicState::join_with(const SymbolicState& other) {
  if (other.is_bottom())
    return;
  if (is_bottom()) {
    *this = other;
    return;
  }

  merge_map(sigma, other.sigma);
  merge_map(mu, other.mu);
  merge_meta(other);
}

void SymbolicState::merge_meta(const SymbolicState& other) {
  if (param_names.empty())
    param_names = other.param_names;
  for (auto* v : other.local_allocs)
    local_allocs.insert(v);
}

// ---------------------------------------------------------------------------
// leq: this ⊑ other
// ---------------------------------------------------------------------------

static bool val_leq(const SymbolicValue& a, const SymbolicValue& b) {
  if (b.is_top())
    return true; // anything ⊑ TOP
  if (a.is_top())
    return false; // TOP ⊑ non-TOP → false
  return a == b;
}

bool SymbolicState::leq(const SymbolicState& other) const {
  if (is_bottom())
    return true;
  if (other.is_bottom())
    return false;

  auto map_leq = [](const auto& left, const auto& right) {
    // It is sufficient to inspect non-TOP entries in right. A missing entry
    // in either map denotes TOP.
    for (auto it = right.begin(); it != right.end(); ++it) {
      if (it->second.is_top())
        continue;
      auto left_it = left.find(it->first);
      if (left_it == left.end() || !val_leq(left_it->second, it->second)) {
        return false;
      }
    }
    return true;
  };

  if (!map_leq(sigma, other.sigma))
    return false;
  if (!map_leq(mu, other.mu))
    return false;
  return true;
}

bool SymbolicState::is_top() const {
  if (_bottom)
    return false;
  for (auto it = sigma.begin(); it != sigma.end(); ++it) {
    if (!it->second.is_top())
      return false;
  }
  for (auto it = mu.begin(); it != mu.end(); ++it) {
    if (!it->second.is_top())
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// eval: look up an ar::Value in σ
// ---------------------------------------------------------------------------

SymbolicValue SymbolicState::eval(ar::Value* v) const {
  if (!v)
    return SymbolicValue::top();

  if (auto* iv = ar::dyn_cast< ar::InternalVariable >(v)) {
    auto it = sigma.find(iv);
    if (it != sigma.end())
      return it->second;
    return SymbolicValue::top();
  }
  if (auto* lv = ar::dyn_cast< ar::LocalVariable >(v)) {
    auto it = sigma.find(lv);
    if (it != sigma.end())
      return it->second;
    return SymbolicValue::top();
  }
  if (auto* ic = ar::dyn_cast< ar::IntegerConstant >(v)) {
    auto zn = ic->value().to_z_number();
    if (zn.fits< int64_t >()) {
      return SymbolicValue::from_expr(SymbolicExpr::from_const(
                                          static_cast< int >(
                                              zn.to< int64_t >())),
                                      zn == 0 || zn == 1);
    }
    return SymbolicValue::top();
  }
  if (ar::isa< ar::NullConstant >(v))
    return SymbolicValue::null();
  return SymbolicValue::top();
}

// ---------------------------------------------------------------------------
// load_mem / store_mem
// ---------------------------------------------------------------------------

SymbolicValue SymbolicState::load_mem(const SymbolicValue& addr) const {
  if (const SymbolicLoc* loc = addr.get_loc()) {
    auto it = mu.find(*loc);
    if (it != mu.end())
      return it->second;
    return SymbolicValue::top();
  }
  return SymbolicValue::top();
}

void SymbolicState::store_mem(const SymbolicValue& addr,
                              const SymbolicValue& val) {
  if (const SymbolicLoc* loc = addr.get_loc()) {
    mu[*loc] = val;
  }
  // If addr is TOP or Expr, we can't track the store → nothing updated.
}

// ---------------------------------------------------------------------------
// Transfer functions
// ---------------------------------------------------------------------------

void SymbolicState::exec(ar::Statement* stmt) {
  if (auto* sa = ar::dyn_cast< ar::Assignment >(stmt)) {
    exec_assignment(sa);
  } else if (auto* su = ar::dyn_cast< ar::UnaryOperation >(stmt)) {
    exec_unary_op(su);
  } else if (auto* sb = ar::dyn_cast< ar::BinaryOperation >(stmt)) {
    exec_binary_op(sb);
  } else if (auto* salloc = ar::dyn_cast< ar::Allocate >(stmt)) {
    exec_allocate(salloc);
  } else if (auto* sps = ar::dyn_cast< ar::PointerShift >(stmt)) {
    exec_pointer_shift(sps);
  } else if (auto* sl = ar::dyn_cast< ar::Load >(stmt)) {
    exec_load(sl);
  } else if (auto* sst = ar::dyn_cast< ar::Store >(stmt)) {
    exec_store(sst);
  } else if (auto* sinv = ar::dyn_cast< ar::Invoke >(stmt)) {
    exec_invoke(sinv);
  } else if (auto* sc = ar::dyn_cast< ar::Call >(stmt)) {
    exec_call(sc);
  } else if (auto* see = ar::dyn_cast< ar::ExtractElement >(stmt)) {
    exec_extract_element(see);
  } else if (auto* sie = ar::dyn_cast< ar::InsertElement >(stmt)) {
    exec_insert_element(sie);
  } else if (auto* ssv = ar::dyn_cast< ar::ShuffleVector >(stmt)) {
    exec_shuffle_vector(ssv);
  }
  // Comparison: path-insensitive, no sigma update (collected separately).
  // LandingPad: result -> TOP.
  if (auto* lp = ar::dyn_cast< ar::LandingPad >(stmt)) {
    sigma[lp->result()] = SymbolicValue::top();
  }
  // Resume, ReturnValue, Unreachable: no sigma update.
}

void SymbolicState::exec_assignment(ar::Assignment* s) {
  sigma[s->result()] = eval(s->operand());
  NullityBoolOrigin origin =
      find_nullity_bool_origin(s->result(), s->parent()->code(), nullptr);
  if (origin.pointer) {
    SymbolicValue pointer = eval(origin.pointer);
    if (pointer.is_loc() && pointer_parameter_index(pointer.loc) >= 0) {
      // The relation holds on BOTH arms; the usual pointwise merge can keep it.
      sigma[s->result()] =
          SymbolicValue::from_bool(pointer.loc, origin.non_null);
    }
  }
}

void SymbolicState::exec_unary_op(ar::UnaryOperation* s) {
  using Op = ar::UnaryOperation::Operator;
  SymbolicValue operand = eval(s->operand());
  switch (s->op()) {
    case Op::ZExt:
      // Widening integer extension: value is preserved.
      sigma[s->result()] = operand;
      break;
    case Op::SExt: {
      auto* type = ar::dyn_cast< ar::IntegerType >(s->operand()->type());
      // Sign extending the bit 1 from i1 yields -1, not the Boolean value 1.
      sigma[s->result()] = type && type->bit_width() == 1 && operand.zero_one
                               ? SymbolicValue::top()
                               : operand;
      break;
    }
    case Op::Bitcast:
      // Pointer/type bitcast: same underlying location/value.
      // A signed i1 result cannot preserve the unsigned {0,1} encoding.
      if (operand.zero_one && s->result()->type()->is_signed_integer() &&
          ar::cast< ar::IntegerType >(s->result()->type())->bit_width() == 1) {
        sigma[s->result()] = SymbolicValue::top();
      } else {
        sigma[s->result()] = operand;
      }
      break;
    case Op::UTrunc:
    case Op::STrunc: {
      // Preserve i*→ui1 truncation only with a proven {0,1} operand range.
      // This handles the standard bool-param ABI
      // pattern: zext(i1 param → iN) followed by utrunc(iN → i1) is a no-op
      // when the iN value is always in {0, 1}.
      auto* rty = ar::dyn_cast< ar::IntegerType >(s->result()->type());
      if (rty && rty->is_unsigned() && rty->bit_width() == 1 &&
          operand.zero_one) {
        sigma[s->result()] = operand;
      } else {
        sigma[s->result()] = SymbolicValue::top();
      }
      break;
    }
    default:
      sigma[s->result()] = SymbolicValue::top();
      break;
  }
}

void SymbolicState::exec_binary_op(ar::BinaryOperation* s) {
  using Op = ar::BinaryOperation::Operator;
  SymbolicValue lv = eval(s->left());
  SymbolicValue rv = eval(s->right());

  const SymbolicExpr* le = lv.get_expr();
  const SymbolicExpr* re = rv.get_expr();

  if (!le || !re) {
    sigma[s->result()] = SymbolicValue::top();
    return;
  }

  switch (s->op()) {
    case Op::UAdd:
    case Op::SAdd:
      if (!s->has_no_wrap()) {
        sigma[s->result()] = SymbolicValue::top();
        break;
      }
      sigma[s->result()] = SymbolicValue::from_expr(*le + *re);
      break;
    case Op::USub:
    case Op::SSub:
      if (!s->has_no_wrap()) {
        sigma[s->result()] = SymbolicValue::top();
        break;
      }
      sigma[s->result()] = SymbolicValue::from_expr(*le - *re);
      break;
    case Op::UMul:
    case Op::SMul:
      if (!s->has_no_wrap()) {
        sigma[s->result()] = SymbolicValue::top();
        break;
      }
      if (le->is_const()) {
        sigma[s->result()] = SymbolicValue::from_expr(*re * le->constant);
      } else if (re->is_const()) {
        sigma[s->result()] = SymbolicValue::from_expr(*le * re->constant);
      } else {
        sigma[s->result()] = SymbolicValue::top();
      }
      break;
    case Op::UShl:
    case Op::SShl:
      if (s->has_no_wrap() && re->is_const()) {
        int shift = re->constant;
        if (shift >= 0 && shift < 32) {
          sigma[s->result()] = SymbolicValue::from_expr(*le * (1 << shift));
        } else {
          sigma[s->result()] = SymbolicValue::top();
        }
      } else {
        sigma[s->result()] = SymbolicValue::top();
      }
      break;
    default:
      sigma[s->result()] = SymbolicValue::top();
      break;
  }
}

void SymbolicState::exec_allocate(ar::Allocate* s) {
  // allocate T, n, align a → result is a pointer to local storage.
  // σ[result] = SymbolicLoc(result)  (the alloca itself is the location)
  ar::LocalVariable* res = s->result();
  SymbolicLoc loc;
  loc.base = res;
  loc.callee_local = true;
  sigma[res] = SymbolicValue::from_loc(loc);
  local_allocs.insert(res);
  // μ[res] is initially unset (uninitialized); will be updated by store.
}

void SymbolicState::exec_load(ar::Load* s) {
  SymbolicValue addr = eval(s->operand());
  sigma[s->result()] = load_mem(addr);
}

void SymbolicState::exec_store(ar::Store* s) {
  SymbolicValue addr = eval(s->pointer());
  SymbolicValue val = eval(s->value());
  store_mem(addr, val);
}

void SymbolicState::exec_call(ar::Call* s) {
  if (s->has_result()) {
    sigma[s->result()] = SymbolicValue::top();
  }
}

// PointerShift: result = base + sum(factor_i * value_i).
// Preserve a parameter-origin affine offset as Loc(B, delta). If an offset
// term is not representable, the result location is TOP.
void SymbolicState::exec_pointer_shift(ar::PointerShift* s) {
  SymbolicValue base = eval(s->pointer());
  const SymbolicLoc* base_loc = base.get_loc();
  if (!base_loc) {
    sigma[s->result()] = SymbolicValue::top();
    return;
  }

  SymbolicLoc result = *base_loc;
  for (auto it = s->term_begin(), et = s->term_end(); it != et; ++it) {
    ar::PointerShift::Term term = *it;
    auto factor = term.first.to_z_number();
    SymbolicValue offset_value = eval(term.second);
    const SymbolicExpr* offset_expr = offset_value.get_expr();
    if (!factor.fits< int >() || !offset_expr) {
      sigma[s->result()] = SymbolicValue::top();
      return;
    }
    result.offset = result.offset + (*offset_expr * factor.to< int >());
  }
  sigma[s->result()] = SymbolicValue::from_loc(result);
}

// Invoke behaves like Call but also has an exception destination edge.
// We model it conservatively: result -> TOP, exception path handled via edge.
void SymbolicState::exec_invoke(ar::Invoke* s) {
  if (s->has_result()) {
    sigma[s->result()] = SymbolicValue::top();
  }
}

void SymbolicState::exec_extract_element(ar::ExtractElement* s) {
  sigma[s->result()] = SymbolicValue::top();
}

void SymbolicState::exec_insert_element(ar::InsertElement* s) {
  sigma[s->result()] = SymbolicValue::top();
}

void SymbolicState::exec_shuffle_vector(ar::ShuffleVector* s) {
  sigma[s->result()] = SymbolicValue::top();
}

// ---------------------------------------------------------------------------
// Dump
// ---------------------------------------------------------------------------

void SymbolicState::dump(std::ostream& o) const {
  dump_state(*this, o, "", "", "state");
}

// Compact single-line dump for trace output: "sigma:{v->e,...}
// mu:{L[v]->e,...}"
void SymbolicState::dump_compact(std::ostream& o) const {
  if (is_bottom()) {
    o << "[BOTTOM]";
    return;
  }
  o << "sigma:{";
  bool first = true;
  for (auto it2 = sigma.begin(); it2 != sigma.end(); ++it2) {
    if (!first)
      o << ", ";
    first = false;
    ar::Variable* var = it2->first;
    if (var->has_name())
      o << var->name();
    else
      o << "?";
    o << "=>";
    dump_sym_val(o, it2->second, param_names);
  }
  o << "} mu:{";
  first = true;
  for (auto it2 = mu.begin(); it2 != mu.end(); ++it2) {
    if (!first)
      o << ", ";
    first = false;
    const SymbolicLoc& loc = it2->first;
    dump_sym_loc(o, loc, param_names);
    o << "=>";
    dump_sym_val(o, it2->second, param_names);
  }
  o << "}";
}

void dump_state(const SymbolicState& s,
                std::ostream& o,
                const std::string& function_name,
                const std::string& block_name,
                const std::string& label) {
  o << "  [" << label << " @ " << block_name << "]\n";
  if (s.is_bottom()) {
    o << "    ⊥ (unreachable)\n";
    return;
  }
  o << "    σ:\n";
  for (auto it2 = s.sigma.begin(); it2 != s.sigma.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    o << "      ";
    if (var->has_name())
      o << var->name();
    else
      o << "?";
    o << " -> ";
    dump_sym_val(o, val, s.param_names);
    o << "\n";
  }
  o << "    mu:\n";
  for (auto it2 = s.mu.begin(); it2 != s.mu.end(); ++it2) {
    const SymbolicLoc& loc = it2->first;
    const SymbolicValue& val = it2->second;
    o << "      ";
    dump_sym_loc(o, loc, s.param_names);
    o << " -> ";
    dump_sym_val(o, val, s.param_names);
    o << "\n";
  }
}

// ---------------------------------------------------------------------------
// SymbolicConstraint
// ---------------------------------------------------------------------------

static const char* pred_str(ar::Comparison::Predicate p) {
  switch (p) {
    case ar::Comparison::SIEQ:
      return "==";
    case ar::Comparison::SINE:
      return "!=";
    case ar::Comparison::SIGT:
      return ">";
    case ar::Comparison::SIGE:
      return ">=";
    case ar::Comparison::SILT:
      return "<";
    case ar::Comparison::SILE:
      return "<=";
    case ar::Comparison::UIEQ:
      return "u==";
    case ar::Comparison::UINE:
      return "u!=";
    case ar::Comparison::UIGT:
      return "u>";
    case ar::Comparison::UIGE:
      return "u>=";
    case ar::Comparison::UILT:
      return "u<";
    case ar::Comparison::UILE:
      return "u<=";
    default:
      return "??";
  }
}

void SymbolicConstraint::dump(std::ostream& o,
                              const std::vector< std::string >& names) const {
  if (kind == KTrue) {
    o << "true";
    return;
  }
  if (kind == KFalse) {
    o << "false";
    return;
  }
  if (kind == KNullity) {
    o << (non_null ? "NonNull(" : "IsNull(");
    SymbolicExpr::from_param(pointer_parameter).dump(o, names);
    o << ")";
    return;
  }
  left.dump(o, names);
  o << " " << pred_str(predicate) << " ";
  right.dump(o, names);
}

// Substitute sigma into a Comparison to get a parameter-based constraint.
// Pointer null comparisons and exact Boolean-origin comparisons are supported.
SymbolicConstraint subst_constraint(ar::Comparison* cmp,
                                    const SymbolicState& s) {
  using P = ar::Comparison;
  if (cmp->predicate() == P::PEQ || cmp->predicate() == P::PNE) {
    if (ar::isa< ar::NullConstant >(cmp->right())) {
      return nullity_constraint(s.eval(cmp->left()),
                                cmp->predicate() == P::PNE);
    }
    if (ar::isa< ar::NullConstant >(cmp->left())) {
      return nullity_constraint(s.eval(cmp->right()),
                                cmp->predicate() == P::PNE);
    }
    return SymbolicConstraint::make_true();
  }
  if (!cmp->is_integer_predicate())
    return SymbolicConstraint::make_true();
  SymbolicValue lv = s.eval(cmp->left());
  SymbolicValue rv = s.eval(cmp->right());
  bool equality = cmp->predicate() == P::SIEQ || cmp->predicate() == P::UIEQ;
  bool inequality = cmp->predicate() == P::SINE || cmp->predicate() == P::UINE;
  if (equality || inequality) {
    const SymbolicValue* boolean = lv.is_bool()   ? &lv
                                   : rv.is_bool() ? &rv
                                                  : nullptr;
    const SymbolicValue& constant = lv.is_bool() ? rv : lv;
    if (boolean && constant.is_expr() && constant.expr.is_const() &&
        (constant.expr.constant == 0 || constant.expr.constant == 1)) {
      bool truth = (constant.expr.constant == 1) == equality;
      return nullity_constraint(SymbolicValue::from_loc(boolean->loc),
                                truth ? boolean->non_null : !boolean->non_null);
    }
  }
  const SymbolicExpr* le = lv.get_expr();
  const SymbolicExpr* re = rv.get_expr();
  if (!le || !re)
    return SymbolicConstraint::make_true();
  return SymbolicConstraint::make_param(cmp->predicate(), *le, *re);
}

int pointer_parameter_index(const SymbolicLoc& pointer) {
  if (!pointer.base || pointer.callee_local || !pointer.offset.is_const() ||
      pointer.offset.constant != 0)
    return -1;
  auto* variable = ar::dyn_cast< ar::InternalVariable >(pointer.base);
  if (!variable)
    return -1;
  ar::Function* function = variable->code()->function_or_null();
  if (!function)
    return -1;
  int index = 0;
  for (auto it = function->param_begin(); it != function->param_end();
       ++it, ++index) {
    if (*it == variable && variable->type()->is_pointer())
      return index;
  }
  return -1;
}

SymbolicConstraint nullity_constraint(const SymbolicValue& pointer,
                                      bool non_null) {
  if (pointer.is_null()) {
    return non_null ? SymbolicConstraint::make_false()
                    : SymbolicConstraint::make_true();
  }
  if (pointer.is_loc()) {
    int index = pointer_parameter_index(pointer.loc);
    if (index >= 0)
      return SymbolicConstraint::make_nullity(index, non_null);
  }
  return SymbolicConstraint::make_true();
}

bool boolean_constraint_value(const SymbolicConstraint& constraint,
                              int& parameter,
                              bool& truth) {
  using P = ar::Comparison;
  if (constraint.kind != SymbolicConstraint::KParam)
    return false;
  bool equality =
      constraint.predicate == P::SIEQ || constraint.predicate == P::UIEQ;
  bool inequality =
      constraint.predicate == P::SINE || constraint.predicate == P::UINE;
  if (!equality && !inequality)
    return false;
  const SymbolicExpr* expression = &constraint.left;
  const SymbolicExpr* constant = &constraint.right;
  if (expression->is_const())
    std::swap(expression, constant);
  if (expression->coeffs.size() != 1 ||
      expression->coeffs.begin()->second != 1 || expression->constant != 0 ||
      !constant->is_const() ||
      (constant->constant != 0 && constant->constant != 1))
    return false;
  parameter = expression->coeffs.begin()->first;
  truth = (constant->constant == 1) == equality;
  return true;
}

namespace {

bool reachable_avoiding(ar::BasicBlock* entry,
                        ar::BasicBlock* target,
                        ar::BasicBlock* avoid) {
  std::vector< ar::BasicBlock* > pending{entry};
  std::unordered_set< ar::BasicBlock* > seen;
  while (!pending.empty()) {
    ar::BasicBlock* block = pending.back();
    pending.pop_back();
    if (block == avoid || !seen.insert(block).second)
      continue;
    if (block == target)
      return true;
    for (auto it = block->successor_begin(); it != block->successor_end();
         ++it) {
      pending.push_back(*it);
    }
  }
  return false;
}

} // anonymous namespace

NullityBoolOrigin find_nullity_bool_origin(ar::Value* boolean,
                                           ar::Code* code,
                                           ar::BasicBlock* use) {
  NullityBoolOrigin unknown;
  auto* type = ar::dyn_cast< ar::IntegerType >(boolean->type());
  if (!type || !type->is_unsigned() || type->bit_width() != 1)
    return unknown;
  std::vector< ar::Assignment* > definitions;
  for (ar::BasicBlock* block : *code) {
    for (ar::Statement* stmt : *block) {
      if (stmt->result_or_null() != boolean)
        continue;
      auto* assign = ar::dyn_cast< ar::Assignment >(stmt);
      if (!assign)
        return unknown;
      definitions.push_back(assign);
    }
  }
  if (definitions.size() < 2)
    return unknown;
  NullityBoolOrigin result;
  std::unordered_set< ar::BasicBlock* > definition_blocks;
  for (auto* assign : definitions) {
    ar::BasicBlock* block = assign->parent();
    auto it = block->begin();
    if (it == block->end())
      return unknown;
    auto* comparison = ar::dyn_cast< ar::Comparison >(*it++);
    if (!comparison || it == block->end() || *it++ != assign ||
        it != block->end()) {
      return unknown;
    }
    auto predicate = comparison->predicate();
    if (predicate != ar::Comparison::PEQ && predicate != ar::Comparison::PNE)
      return unknown;
    ar::Value* pointer = nullptr;
    if (ar::isa< ar::NullConstant >(comparison->right()))
      pointer = comparison->left();
    else if (ar::isa< ar::NullConstant >(comparison->left()))
      pointer = comparison->right();
    if (!pointer || !ar::isa< ar::Variable >(pointer) ||
        !pointer->type()->is_pointer())
      return unknown;
    auto* constant = ar::dyn_cast< ar::IntegerConstant >(assign->operand());
    if (!constant)
      return unknown;
    bool value = !constant->value().is_zero();
    bool non_null = (predicate == ar::Comparison::PNE) == value;
    if (result.pointer &&
        (result.pointer != pointer || result.non_null != non_null)) {
      return unknown;
    }
    result.pointer = pointer;
    result.non_null = non_null;
    definition_blocks.insert(block);
    // Limit recognition to acyclic Boolean constructions. A unique AR
    // definition in a loop can still denote different dynamic pointer values.
    for (auto successor = block->successor_begin();
         successor != block->successor_end();
         ++successor) {
      if (reachable_avoiding(*successor, block, nullptr))
        return unknown;
    }
  }
  if (use) {
    // Every path reaching this use must pass a checked assignment. This also
    // covers branch replication when several bool arguments are evaluated.
    std::vector< ar::BasicBlock* > pending{code->entry_block()};
    std::unordered_set< ar::BasicBlock* > seen;
    while (!pending.empty()) {
      ar::BasicBlock* block = pending.back();
      pending.pop_back();
      if (definition_blocks.count(block) || !seen.insert(block).second)
        continue;
      if (block == use)
        return unknown;
      for (auto it = block->successor_begin(); it != block->successor_end();
           ++it) {
        pending.push_back(*it);
      }
    }
  }
  unsigned pointer_definitions = 0;
  for (ar::BasicBlock* block : *code) {
    for (ar::Statement* stmt : *block) {
      if (stmt->result_or_null() == result.pointer && ++pointer_definitions > 1)
        return unknown;
    }
  }
  // A formal already has an entry definition, even though it has no defining
  // statement. One assignment to it would therefore be a redefinition.
  if (pointer_definitions != 0) {
    if (ar::Function* function = code->function_or_null()) {
      for (auto it = function->param_begin(); it != function->param_end();
           ++it) {
        if (*it == result.pointer)
          return unknown;
      }
    }
  }
  return result;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
