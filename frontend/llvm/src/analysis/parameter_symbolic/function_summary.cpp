#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>

#include <algorithm>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

namespace {

bool same_constraint(const SymbolicConstraint& lhs,
                     const SymbolicConstraint& rhs) {
  if (lhs.kind != rhs.kind)
    return false;
  if (lhs.kind == SymbolicConstraint::KTrue ||
      lhs.kind == SymbolicConstraint::KFalse ||
      lhs.kind == SymbolicConstraint::KUnknown)
    return true;
  if (lhs.kind == SymbolicConstraint::KNullity) {
    return lhs.pointer_parameter == rhs.pointer_parameter &&
           lhs.non_null == rhs.non_null;
  }
  if (lhs.kind == SymbolicConstraint::KTyped)
    return lhs.predicate == rhs.predicate && lhs.typed_left == rhs.typed_left &&
           lhs.typed_right == rhs.typed_right;
  return lhs.predicate == rhs.predicate && lhs.left == rhs.left &&
         lhs.right == rhs.right;
}

bool opposite(const SymbolicConstraint& a, const SymbolicConstraint& b) {
  if (a.kind != SymbolicConstraint::KTyped || b.kind != a.kind ||
      a.typed_left != b.typed_left || a.typed_right != b.typed_right)
    return false;
  using P = ar::Comparison;
  switch (a.predicate) {
    case P::SIEQ:
      return b.predicate == P::SINE;
    case P::SINE:
      return b.predicate == P::SIEQ;
    case P::UIEQ:
      return b.predicate == P::UINE;
    case P::UINE:
      return b.predicate == P::UIEQ;
    case P::SIGT:
      return b.predicate == P::SILE;
    case P::SIGE:
      return b.predicate == P::SILT;
    case P::SILT:
      return b.predicate == P::SIGE;
    case P::SILE:
      return b.predicate == P::SIGT;
    case P::UIGT:
      return b.predicate == P::UILE;
    case P::UIGE:
      return b.predicate == P::UILT;
    case P::UILT:
      return b.predicate == P::UIGE;
    case P::UILE:
      return b.predicate == P::UIGT;
    default:
      return false;
  }
}

bool constant_truth(const SymbolicConstraint& c) {
  auto l = c.typed_left.node().constant.to_z_number();
  auto r = c.typed_right.node().constant.to_z_number();
  using P = ar::Comparison;
  switch (c.predicate) {
    case P::SIEQ:
    case P::UIEQ:
      return l == r;
    case P::SINE:
    case P::UINE:
      return l != r;
    case P::SIGT:
    case P::UIGT:
      return l > r;
    case P::SIGE:
    case P::UIGE:
      return l >= r;
    case P::SILT:
    case P::UILT:
      return l < r;
    case P::SILE:
    case P::UILE:
      return l <= r;
    default:
      return false;
  }
}

bool contains_opposite_nullity(
    const SymbolicCondition::Conjunction& conjunction,
    const SymbolicConstraint& constraint) {
  for (const SymbolicConstraint& existing : conjunction) {
    if (existing.kind == SymbolicConstraint::KNullity &&
        existing.pointer_parameter == constraint.pointer_parameter &&
        existing.non_null != constraint.non_null) {
      return true;
    }
  }
  return false;
}

bool contains_constraint(const SymbolicCondition::Conjunction& conjunction,
                         const SymbolicConstraint& constraint) {
  return std::find_if(conjunction.begin(),
                      conjunction.end(),
                      [&](const SymbolicConstraint& existing) {
                        return same_constraint(existing, constraint);
                      }) != conjunction.end();
}

bool same_conjunction(const SymbolicCondition::Conjunction& lhs,
                      const SymbolicCondition::Conjunction& rhs) {
  if (lhs.size() != rhs.size())
    return false;
  for (const SymbolicConstraint& constraint : lhs) {
    if (!contains_constraint(rhs, constraint))
      return false;
  }
  return true;
}

bool conjunction_implies(const SymbolicCondition::Conjunction& lhs,
                         const SymbolicCondition::Conjunction& rhs) {
  // A conjunction implies another conjunction when it contains all of the
  // other's atoms. For an OR, the stronger conjunction is redundant.
  if (lhs.size() != rhs.size()) {
    for (const auto& c : lhs)
      if (c.kind == SymbolicConstraint::KTyped &&
          (!c.typed_left.is_variable() || !c.typed_right.is_constant()))
        return false;
  }
  return std::all_of(rhs.begin(),
                     rhs.end(),
                     [&](const SymbolicConstraint& constraint) {
                       return contains_constraint(lhs, constraint);
                     });
}

} // end anonymous namespace

// ---------------------------------------------------------------------------
// SymbolicCondition
// ---------------------------------------------------------------------------

SymbolicCondition SymbolicCondition::make_false() {
  return SymbolicCondition{};
}

SymbolicCondition SymbolicCondition::make_true() {
  SymbolicCondition condition;
  condition.disjuncts.emplace_back();
  return condition;
}

bool SymbolicCondition::is_true() const {
  return this->disjuncts.size() == 1 && this->disjuncts.front().empty();
}

void SymbolicCondition::join_with(const SymbolicCondition& other) {
  if (this->is_true() || other.is_false())
    return;
  if (other.is_true()) {
    *this = make_true();
    return;
  }

  for (const Conjunction& incoming : other.disjuncts) {
    auto subsuming =
        std::find_if(this->disjuncts.begin(),
                     this->disjuncts.end(),
                     [&](const Conjunction& existing) {
                       return conjunction_implies(incoming, existing);
                     });
    if (subsuming != this->disjuncts.end())
      continue;

    this->disjuncts.erase(std::remove_if(this->disjuncts.begin(),
                                         this->disjuncts.end(),
                                         [&](const Conjunction& existing) {
                                           return conjunction_implies(existing,
                                                                      incoming);
                                         }),
                          this->disjuncts.end());
    this->disjuncts.push_back(incoming);
  }
}

void SymbolicCondition::meet_with(const SymbolicCondition& other) {
  if (this->is_false() || other.is_true())
    return;
  if (other.is_false()) {
    *this = make_false();
    return;
  }
  if (this->is_true()) {
    *this = other;
    return;
  }

  SymbolicCondition result = make_false();
  for (const Conjunction& left : this->disjuncts) {
    for (const Conjunction& right : other.disjuncts) {
      SymbolicCondition term = make_true();
      for (const SymbolicConstraint& constraint : left) {
        term.add_constraint(constraint);
      }
      for (const SymbolicConstraint& constraint : right) {
        term.add_constraint(constraint);
      }
      result.join_with(term);
    }
  }
  *this = std::move(result);
}

void SymbolicCondition::add_constraint(const SymbolicConstraint& constraint) {
  if (constraint.kind == SymbolicConstraint::KTyped &&
      constraint.typed_left.is_constant() &&
      constraint.typed_right.is_constant()) {
    if (!constant_truth(constraint))
      *this = make_false();
    return;
  }
  if (constraint.kind == SymbolicConstraint::KTyped) {
    auto end =
        std::remove_if(disjuncts.begin(),
                       disjuncts.end(),
                       [&](const Conjunction& conjunction) {
                         return std::any_of(conjunction.begin(),
                                            conjunction.end(),
                                            [&](const SymbolicConstraint& c) {
                                              return opposite(c, constraint);
                                            });
                       });
    disjuncts.erase(end, disjuncts.end());
  }
  if (constraint.kind == SymbolicConstraint::KTrue || this->is_false())
    return;
  if (constraint.kind == SymbolicConstraint::KFalse ||
      constraint.kind == SymbolicConstraint::KUnknown) {
    *this = make_false();
    return;
  }
  if (constraint.kind == SymbolicConstraint::KNullity) {
    auto new_end =
        std::remove_if(this->disjuncts.begin(),
                       this->disjuncts.end(),
                       [&](const Conjunction& conjunction) {
                         return contains_opposite_nullity(conjunction,
                                                          constraint);
                       });
    this->disjuncts.erase(new_end, this->disjuncts.end());
  }
  for (Conjunction& conjunction : this->disjuncts) {
    if (!contains_constraint(conjunction, constraint)) {
      conjunction.push_back(constraint);
    }
  }
}

bool SymbolicCondition::equals(const SymbolicCondition& other) const {
  if (this->disjuncts.size() != other.disjuncts.size())
    return false;
  for (const Conjunction& conjunction : this->disjuncts) {
    auto matching =
        std::find_if(other.disjuncts.begin(),
                     other.disjuncts.end(),
                     [&](const Conjunction& candidate) {
                       return same_conjunction(conjunction, candidate);
                     });
    if (matching == other.disjuncts.end())
      return false;
  }
  return true;
}

void SymbolicCondition::dump(std::ostream& o,
                             const std::vector< std::string >& param_names,
                             const std::string& indent) const {
  if (this->is_false()) {
    o << indent << "false\n";
    return;
  }
  if (this->is_true()) {
    o << indent << "true\n";
    return;
  }

  for (std::size_t i = 0; i < this->disjuncts.size(); ++i) {
    o << indent;
    if (this->disjuncts.size() > 1)
      o << "(";
    const Conjunction& conjunction = this->disjuncts[i];
    for (std::size_t j = 0; j < conjunction.size(); ++j) {
      if (j != 0)
        o << " && ";
      conjunction[j].dump(o, param_names);
    }
    if (this->disjuncts.size() > 1)
      o << ")";
    if (i + 1 != this->disjuncts.size())
      o << " ||";
    o << "\n";
  }
}

// ---------------------------------------------------------------------------
// NormalSummary::dump
// ---------------------------------------------------------------------------

void NormalSummary::dump(std::ostream& o) const {
  if (!available) {
    o << "  [normal_exit] unavailable\n";
    return;
  }
  if (!has_exit) {
    o << (complete ? "  [normal_exit] none (Complete)\n"
                   : "  [normal_exit] no certified region (Partial)\n");
    return;
  }
  o << "  [normal_exit]\n";

  o << "    coverage: " << (complete ? "Complete" : "Partial") << "\n";
  o << "    C_normal:\n";
  condition.dump(o, param_names, "      ");

  o << "    mu_normal_exit (caller-visible):\n";
  if (mu_exit.empty()) {
    o << "      (empty)\n";
  } else {
    for (const auto& kv : mu_exit) {
      o << "      ";
      dump_sym_loc(o, kv.first, param_names);
      o << " -> ";
      dump_sym_val(o, kv.second, param_names);
      o << "\n";
    }
  }
}

// ---------------------------------------------------------------------------
// ExceptionSummary::dump
// ---------------------------------------------------------------------------

void ExceptionSummary::dump(std::ostream& o) const {
  if (!available) {
    o << "  [exception_exit] unavailable\n";
    return;
  }
  if (!has_exit) {
    o << (complete ? "  [exception_exit] none (Complete)\n"
                   : "  [exception_exit] no certified region (Partial)\n");
    return;
  }
  o << "  [exception_exit]\n";

  o << "    coverage: " << (complete ? "Complete" : "Partial") << "\n";
  o << "    C_exception:\n";
  condition.dump(o, param_names, "      ");

  o << "    mu_exception_exit (caller-visible):\n";
  if (mu_exit.empty()) {
    o << "      (empty)\n";
  } else {
    for (const auto& kv : mu_exit) {
      o << "      ";
      dump_sym_loc(o, kv.first, param_names);
      o << " -> ";
      dump_sym_val(o, kv.second, param_names);
      o << "\n";
    }
  }
}

// ---------------------------------------------------------------------------
// FunctionSummary::dump
// ---------------------------------------------------------------------------

void FunctionSummary::dump(std::ostream& o) const {
  const std::string& fname =
      (function != nullptr) ? function->name() : "<unknown>";
  o << "============================\n";
  o << "Function: " << fname << "\n";
  o << "============================\n";
  normal.dump(o);
  exception.dump(o);
  o << "\n";
}

// ---------------------------------------------------------------------------
// SummaryStore
// ---------------------------------------------------------------------------

SummaryStore::SummaryStore(SummaryStore&& other) noexcept {
  std::lock_guard< std::mutex > lock(other._mutex);
  this->_store = std::move(other._store);
}

SummaryStore& SummaryStore::operator=(SummaryStore&& other) noexcept {
  if (this == &other)
    return *this;
  std::unique_lock< std::mutex > lhs_lock(this->_mutex, std::defer_lock);
  std::unique_lock< std::mutex > rhs_lock(other._mutex, std::defer_lock);
  std::lock(lhs_lock, rhs_lock);
  this->_store = std::move(other._store);
  return *this;
}

void SummaryStore::put(ar::Function* fun, FunctionSummary summary) {
  std::lock_guard< std::mutex > lock(this->_mutex);
  this->_store.emplace(fun, std::move(summary));
}

const FunctionSummary* SummaryStore::get(ar::Function* fun) const {
  std::lock_guard< std::mutex > lock(this->_mutex);
  auto it = this->_store.find(fun);
  if (it == this->_store.end())
    return nullptr;
  return &it->second;
}

bool SummaryStore::has(ar::Function* fun) const {
  std::lock_guard< std::mutex > lock(this->_mutex);
  return this->_store.count(fun) != 0;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
