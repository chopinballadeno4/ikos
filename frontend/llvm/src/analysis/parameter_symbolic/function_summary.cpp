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

// Exact truth sets for scalar parameter/constant comparisons. Using two
// intervals for != preserves its interior hole, unlike an interval hull.
using Segment = std::pair< core::ZNumber, core::ZNumber >;
bool scalar_regions(const SymbolicConstraint& c, std::vector< Segment >& out) {
  if (c.kind != SymbolicConstraint::KTyped || !c.typed_left.is_variable() ||
      !c.typed_right.is_constant() ||
      c.typed_left.bit_width() != c.typed_right.bit_width() ||
      c.typed_left.sign() != c.typed_right.sign())
    return false;
  using P = ar::Comparison;
  bool signed_predicate;
  switch (c.predicate) {
    case P::SIEQ:
    case P::SINE:
    case P::SIGT:
    case P::SIGE:
    case P::SILT:
    case P::SILE:
      signed_predicate = true;
      break;
    case P::UIEQ:
    case P::UINE:
    case P::UIGT:
    case P::UIGE:
    case P::UILT:
    case P::UILE:
      signed_predicate = false;
      break;
    default:
      return false;
  }
  if (signed_predicate != (c.typed_left.sign() == core::Signed))
    return false;
  auto width = c.typed_left.bit_width();
  auto sign = c.typed_left.sign();
  auto lo = core::MachineInt::min(width, sign).to_z_number();
  auto hi = core::MachineInt::max(width, sign).to_z_number();
  auto k = c.typed_right.node().constant.to_z_number();
  auto append = [&](core::ZNumber l, core::ZNumber h) {
    if (l <= h)
      out.emplace_back(std::move(l), std::move(h));
  };
  switch (c.predicate) {
    case P::SIEQ:
    case P::UIEQ:
      append(k, k);
      break;
    case P::SINE:
    case P::UINE:
      append(lo, k - 1);
      append(k + 1, hi);
      break;
    case P::SIGT:
    case P::UIGT:
      append(k + 1, hi);
      break;
    case P::SIGE:
    case P::UIGE:
      append(k, hi);
      break;
    case P::SILT:
    case P::UILT:
      append(lo, k - 1);
      break;
    case P::SILE:
    case P::UILE:
      append(lo, k);
      break;
    default:
      return false;
  }
  return true;
}

bool atom_implies(const SymbolicConstraint& lhs,
                  const SymbolicConstraint& rhs) {
  if (same_constraint(lhs, rhs))
    return true;
  if (lhs.kind != SymbolicConstraint::KTyped || rhs.kind != lhs.kind ||
      lhs.typed_left != rhs.typed_left)
    return false;
  std::vector< Segment > l, r;
  if (!scalar_regions(lhs, l) || !scalar_regions(rhs, r))
    return false;
  return std::all_of(l.begin(), l.end(), [&](const Segment& a) {
    return std::any_of(r.begin(), r.end(), [&](const Segment& b) {
      return b.first <= a.first && a.second <= b.second;
    });
  });
}

bool tighten_excluded_endpoint(SymbolicConstraint& bound,
                               const SymbolicConstraint& excluded) {
  using P = ar::Comparison;
  std::vector< Segment > regions;
  if (!scalar_regions(bound, regions) ||
      excluded.kind != SymbolicConstraint::KTyped ||
      bound.typed_left != excluded.typed_left ||
      bound.typed_right != excluded.typed_right)
    return false;
  if (excluded.predicate == P::SINE) {
    if (bound.predicate == P::SIGE) {
      bound.predicate = P::SIGT;
      return true;
    }
    if (bound.predicate == P::SILE) {
      bound.predicate = P::SILT;
      return true;
    }
  } else if (excluded.predicate == P::UINE) {
    if (bound.predicate == P::UIGE) {
      bound.predicate = P::UIGT;
      return true;
    }
    if (bound.predicate == P::UILE) {
      bound.predicate = P::UILT;
      return true;
    }
  }
  return false;
}

bool implies_atom(const SymbolicCondition::Conjunction& lhs,
                  const SymbolicConstraint& rhs) {
  return std::any_of(lhs.begin(), lhs.end(), [&](const SymbolicConstraint& c) {
    return atom_implies(c, rhs);
  });
}

bool conjunction_implies(const SymbolicCondition::Conjunction& lhs,
                         const SymbolicCondition::Conjunction& rhs) {
  // Preserve source nonlinear checks (notably benchmarks 29/30), even when
  // removing the disjunct would otherwise preserve its truth set.
  for (const auto& c : lhs)
    if (c.kind == SymbolicConstraint::KTyped &&
        (!c.typed_left.is_variable() || !c.typed_right.is_constant()) &&
        !contains_constraint(rhs, c))
      return false;
  return std::all_of(rhs.begin(),
                     rhs.end(),
                     [&](const SymbolicConstraint& constraint) {
                       return implies_atom(lhs, constraint);
                     });
}

void normalize_disjunction(std::vector< SymbolicCondition::Conjunction >& dnf) {
  // Every successful rewrite is equivalent. Budget exhaustion simply keeps
  // the remaining atoms/disjuncts; it does not change coverage.
  unsigned budget = 16384;
  bool changed = true;
  while (changed && budget) {
    changed = false;
    for (auto& term : dnf) {
      for (std::size_t i = 0; i < term.size() && budget;) {
        bool redundant = false;
        for (std::size_t j = 0; j < term.size() && budget; ++j) {
          --budget;
          if (i != j && tighten_excluded_endpoint(term[i], term[j]))
            changed = true;
          if (i != j && atom_implies(term[j], term[i])) {
            redundant = true;
            break;
          }
        }
        if (redundant) {
          term.erase(term.begin() + i);
          changed = true;
        } else {
          ++i;
        }
      }
    }
    for (std::size_t i = 0; i < dnf.size() && budget; ++i) {
      for (std::size_t j = 0; j < dnf.size() && budget; ++j) {
        if (i == j)
          continue;
        --budget;
        if (conjunction_implies(dnf[i], dnf[j])) {
          dnf.erase(dnf.begin() + i);
          changed = true;
          // Restart after erasing a disjunct to avoid invalid indices.
          break;
        }
        // K && c OR !c && R == K OR !c && R when K implies R.
        // Only a scalar c may be removed; Rem/Div/USub/UMul stay opaque.
        auto& term = dnf[i];
        bool absorbed = false;
        for (std::size_t k = 0; k < term.size() && budget; ++k) {
          std::vector< Segment > regions;
          if (!scalar_regions(term[k], regions))
            continue;
          const auto& witness = dnf[j];
          for (std::size_t w = 0; w < witness.size() && budget; ++w) {
            --budget;
            if (!opposite(term[k], witness[w]))
              continue;
            bool covered = true;
            for (std::size_t r = 0; r < witness.size() && covered; ++r) {
              if (r == w)
                continue;
              covered = false;
              for (std::size_t t = 0; t < term.size() && budget; ++t) {
                --budget;
                if (t != k && atom_implies(term[t], witness[r])) {
                  covered = true;
                  break;
                }
              }
            }
            if (covered) {
              term.erase(term.begin() + k);
              changed = absorbed = true;
              break;
            }
          }
          if (absorbed)
            break;
        }
        if (absorbed)
          break;
      }
      if (changed)
        break;
    }
  }
  if (std::any_of(dnf.begin(), dnf.end(), [](const auto& term) {
        return term.empty();
      })) {
    dnf.clear();
    dnf.emplace_back();
  }
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
  if (this == &other || this->is_true() || other.is_false())
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
  normalize_disjunction(this->disjuncts);
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
  normalize_disjunction(this->disjuncts);
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
  dump_preconditions(o);
  normal.dump(o);
  exception.dump(o);
  o << "\n";
}

void FunctionSummary::dump_preconditions(std::ostream& o) const {
  if (memory_preconditions.empty())
    return;
  o << "  memory preconditions (coverage is relative to these):\n";
  for (const auto& requirement : memory_preconditions) {
    o << "    Writable(p" << requirement.parameter << "0, ";
    requirement.type->dump(o);
    o << ", " << requirement.type->bit_width() / 8 << " bytes, align "
      << requirement.alignment << ")\n";
  }
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
