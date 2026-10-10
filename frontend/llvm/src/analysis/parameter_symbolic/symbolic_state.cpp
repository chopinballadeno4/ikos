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

    if (ar::isa<ar::IntegerType>(ty) || ar::isa<ar::FloatType>(ty)) {
      s.sigma[p] = SymbolicValue::from_expr(SymbolicExpr::from_param(idx));
    } else if (ar::isa<ar::PointerType>(ty)) {
      SymbolicLoc loc;
      loc.var = p;
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

static void merge_map(SymbolicState::VarMap& dst,
                      const SymbolicState::VarMap& src) {
  // Merge src into dst.
  // Keys only in dst: they stay (variable defined only on one path → keep it;
  //   if the variable is also defined on the other path the PHI assignment
  //   will update it during block transfer).
  // Keys only in src: copy over.
  // Keys in both: MergeVal.
  for (auto it2 = src.begin(); it2 != src.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    auto it = dst.find(var);
    if (it == dst.end()) {
      dst[var] = val;
    } else {
      it->second = merge_val(it->second, val);
    }
  }
}

void SymbolicState::join_with(const SymbolicState& other) {
  if (other.is_bottom()) return;
  if (is_bottom()) { *this = other; return; }

  merge_map(sigma, other.sigma);
  merge_map(mu, other.mu);
  merge_meta(other);
}

void SymbolicState::merge_meta(const SymbolicState& other) {
  if (param_names.empty()) param_names = other.param_names;
  for (auto* v : other.local_allocs) local_allocs.insert(v);
}

// ---------------------------------------------------------------------------
// leq: this ⊑ other
// ---------------------------------------------------------------------------

static bool val_leq(const SymbolicValue& a, const SymbolicValue& b) {
  if (b.is_top()) return true;   // anything ⊑ TOP
  if (a.is_top()) return false;  // TOP ⊑ non-TOP → false
  return a == b;
}

bool SymbolicState::leq(const SymbolicState& other) const {
  if (is_bottom()) return true;
  if (other.is_bottom()) return false;

  for (auto it2 = sigma.begin(); it2 != sigma.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    auto it = other.sigma.find(var);
    if (it == other.sigma.end()) {
      // other doesn't have this var → treat as TOP → ok
      continue;
    }
    if (!val_leq(val, it->second)) return false;
  }
  for (auto it2 = mu.begin(); it2 != mu.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    auto it = other.mu.find(var);
    if (it == other.mu.end()) continue;
    if (!val_leq(val, it->second)) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// eval: look up an ar::Value in σ
// ---------------------------------------------------------------------------

SymbolicValue SymbolicState::eval(ar::Value* v) const {
  if (!v) return SymbolicValue::top();

  if (auto* iv = ar::dyn_cast<ar::InternalVariable>(v)) {
    auto it = sigma.find(iv);
    if (it != sigma.end()) return it->second;
    return SymbolicValue::top();
  }
  if (auto* lv = ar::dyn_cast<ar::LocalVariable>(v)) {
    auto it = sigma.find(lv);
    if (it != sigma.end()) return it->second;
    return SymbolicValue::top();
  }
  if (auto* ic = ar::dyn_cast<ar::IntegerConstant>(v)) {
    auto zn = ic->value().to_z_number();
    if (zn.fits<int64_t>()) {
      return SymbolicValue::from_expr(
          SymbolicExpr::from_const(static_cast<int>(zn.to<int64_t>())));
    }
    return SymbolicValue::top();
  }
  return SymbolicValue::top();
}

// ---------------------------------------------------------------------------
// load_mem / store_mem
// ---------------------------------------------------------------------------

SymbolicValue SymbolicState::load_mem(const SymbolicValue& addr) const {
  if (const SymbolicLoc* loc = addr.get_loc()) {
    auto it = mu.find(loc->var);
    if (it != mu.end()) return it->second;
    return SymbolicValue::top();
  }
  return SymbolicValue::top();
}

void SymbolicState::store_mem(const SymbolicValue& addr,
                              const SymbolicValue& val) {
  if (const SymbolicLoc* loc = addr.get_loc()) {
    mu[loc->var] = val;
  }
  // If addr is TOP or Expr, we can't track the store → nothing updated.
}

// ---------------------------------------------------------------------------
// Transfer functions
// ---------------------------------------------------------------------------

void SymbolicState::exec(ar::Statement* stmt) {
  if (auto* sa = ar::dyn_cast<ar::Assignment>(stmt)) {
    exec_assignment(sa);
  } else if (auto* su = ar::dyn_cast<ar::UnaryOperation>(stmt)) {
    exec_unary_op(su);
  } else if (auto* sb = ar::dyn_cast<ar::BinaryOperation>(stmt)) {
    exec_binary_op(sb);
  } else if (auto* salloc = ar::dyn_cast<ar::Allocate>(stmt)) {
    exec_allocate(salloc);
  } else if (auto* sps = ar::dyn_cast<ar::PointerShift>(stmt)) {
    exec_pointer_shift(sps);
  } else if (auto* sl = ar::dyn_cast<ar::Load>(stmt)) {
    exec_load(sl);
  } else if (auto* sst = ar::dyn_cast<ar::Store>(stmt)) {
    exec_store(sst);
  } else if (auto* sinv = ar::dyn_cast<ar::Invoke>(stmt)) {
    exec_invoke(sinv);
  } else if (auto* sc = ar::dyn_cast<ar::Call>(stmt)) {
    exec_call(sc);
  } else if (auto* see = ar::dyn_cast<ar::ExtractElement>(stmt)) {
    exec_extract_element(see);
  } else if (auto* sie = ar::dyn_cast<ar::InsertElement>(stmt)) {
    exec_insert_element(sie);
  } else if (auto* ssv = ar::dyn_cast<ar::ShuffleVector>(stmt)) {
    exec_shuffle_vector(ssv);
  }
  // Comparison: path-insensitive, no sigma update (collected separately).
  // LandingPad: result -> TOP.
  if (auto* lp = ar::dyn_cast<ar::LandingPad>(stmt)) {
    sigma[lp->result()] = SymbolicValue::top();
  }
  // Resume, ReturnValue, Unreachable: no sigma update.
}

void SymbolicState::exec_assignment(ar::Assignment* s) {
  sigma[s->result()] = eval(s->operand());
}

void SymbolicState::exec_unary_op(ar::UnaryOperation* s) {
  using Op = ar::UnaryOperation::Operator;
  SymbolicValue operand = eval(s->operand());
  switch (s->op()) {
    case Op::ZExt:
    case Op::SExt:
      // Widening integer extension: value is preserved.
      sigma[s->result()] = operand;
      break;
    case Op::Bitcast:
      // Pointer/type bitcast: same underlying location/value.
      sigma[s->result()] = operand;
      break;
    case Op::UTrunc:
    case Op::STrunc:
      sigma[s->result()] = SymbolicValue::top();
      break;
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
    case Op::UAdd: case Op::SAdd:
      sigma[s->result()] = SymbolicValue::from_expr(*le + *re);
      break;
    case Op::USub: case Op::SSub:
      sigma[s->result()] = SymbolicValue::from_expr(*le - *re);
      break;
    case Op::UMul: case Op::SMul:
      if (le->is_const()) {
        sigma[s->result()] = SymbolicValue::from_expr(*re * le->constant);
      } else if (re->is_const()) {
        sigma[s->result()] = SymbolicValue::from_expr(*le * re->constant);
      } else {
        sigma[s->result()] = SymbolicValue::top();
      }
      break;
    case Op::UShl: case Op::SShl:
      if (re->is_const()) {
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
  loc.var = res;
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
  SymbolicValue val  = eval(s->value());
  store_mem(addr, val);
}

void SymbolicState::exec_call(ar::Call* s) {
  if (s->has_result()) {
    sigma[s->result()] = SymbolicValue::top();
  }
}

// PointerShift: result = base + sum(factor_i * value_i).
// If base is a Loc, propagate the same Loc (offset is untracked — over-approx).
// If any index variable is non-constant, result is still same Loc as base
// (we lose field granularity but preserve the base-object identity).
void SymbolicState::exec_pointer_shift(ar::PointerShift* s) {
  SymbolicValue base = eval(s->pointer());
  if (base.is_loc()) {
    // Loc(B, delta): we represent as same base Loc, ignoring numeric offset.
    sigma[s->result()] = base;
  } else {
    sigma[s->result()] = SymbolicValue::top();
  }
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

// Compact single-line dump for trace output: "sigma:{v->e,...} mu:{L[v]->e,...}"
void SymbolicState::dump_compact(std::ostream& o) const {
  if (is_bottom()) { o << "[BOTTOM]"; return; }
  o << "sigma:{";
  bool first = true;
  for (auto it2 = sigma.begin(); it2 != sigma.end(); ++it2) {
    if (!first) o << ", ";
    first = false;
    ar::Variable* var = it2->first;
    if (var->has_name()) o << var->name();
    else o << "?";
    o << "=>";
    dump_sym_val(o, it2->second, param_names);
  }
  o << "} mu:{";
  first = true;
  for (auto it2 = mu.begin(); it2 != mu.end(); ++it2) {
    if (!first) o << ", ";
    first = false;
    ar::Variable* var = it2->first;
    o << "L[";
    if (var->has_name()) o << var->name();
    else o << "?";
    o << "]=>";
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
  if (s.is_bottom()) { o << "    ⊥ (unreachable)\n"; return; }
  o << "    σ:\n";
  for (auto it2 = s.sigma.begin(); it2 != s.sigma.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    o << "      ";
    if (var->has_name()) o << var->name();
    else o << "?";
    o << " -> ";
    dump_sym_val(o, val, s.param_names);
    o << "\n";
  }
  o << "    mu:\n";
  for (auto it2 = s.mu.begin(); it2 != s.mu.end(); ++it2) {
    ar::Variable* var = it2->first;
    const SymbolicValue& val = it2->second;
    o << "      L[";
    if (var->has_name()) o << var->name();
    else o << "?";
    o << "] -> ";
    dump_sym_val(o, val, s.param_names);
    o << "\n";
  }
}

// ---------------------------------------------------------------------------
// SymbolicConstraint
// ---------------------------------------------------------------------------

static const char* pred_str(ar::Comparison::Predicate p) {
  switch (p) {
    case ar::Comparison::SIEQ: return "==";
    case ar::Comparison::SINE: return "!=";
    case ar::Comparison::SIGT: return ">";
    case ar::Comparison::SIGE: return ">=";
    case ar::Comparison::SILT: return "<";
    case ar::Comparison::SILE: return "<=";
    case ar::Comparison::UIEQ: return "u==";
    case ar::Comparison::UINE: return "u!=";
    case ar::Comparison::UIGT: return "u>";
    case ar::Comparison::UIGE: return "u>=";
    case ar::Comparison::UILT: return "u<";
    case ar::Comparison::UILE: return "u<=";
    default: return "??";
  }
}

void SymbolicConstraint::dump(std::ostream& o,
                               const std::vector< std::string >& names) const {
  if (kind == KTrue) { o << "true"; return; }
  left.dump(o, names);
  o << " " << pred_str(predicate) << " ";
  right.dump(o, names);
}

// Substitute sigma into a Comparison to get a parameter-based constraint.
// Only handles integer predicates; returns KTrue otherwise.
SymbolicConstraint subst_constraint(ar::Comparison* cmp,
                                    const SymbolicState& s) {
  if (!cmp->is_integer_predicate()) return SymbolicConstraint::make_true();
  SymbolicValue lv = s.eval(cmp->left());
  SymbolicValue rv = s.eval(cmp->right());
  const SymbolicExpr* le = lv.get_expr();
  const SymbolicExpr* re = rv.get_expr();
  if (!le || !re) return SymbolicConstraint::make_true();
  return SymbolicConstraint::make_param(cmp->predicate(), *le, *re);
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
