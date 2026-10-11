#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

// ---------------------------------------------------------------------------
// NormalSummary::dump
// ---------------------------------------------------------------------------

void NormalSummary::dump(std::ostream& o) const {
  if (!has_exit) {
    o << "  [normal_exit] none\n";
    return;
  }
  o << "  [normal_exit]\n";

  o << "    C_normal:\n";
  if (constraints.empty()) {
    o << "      true\n";
  } else {
    for (const SymbolicConstraint& c : constraints) {
      o << "      ";
      c.dump(o, param_names);
      o << "\n";
    }
  }

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
  if (!has_exit) {
    o << "  [exception_exit] none\n";
    return;
  }
  o << "  [exception_exit]\n";

  o << "    C_exception:\n";
  if (constraints.empty()) {
    o << "      true\n";
  } else {
    for (const SymbolicConstraint& c : constraints) {
      o << "      ";
      c.dump(o, param_names);
      o << "\n";
    }
  }

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

void SummaryStore::put(ar::Function* fun, FunctionSummary summary) {
  _store[fun] = std::move(summary);
}

const FunctionSummary* SummaryStore::get(ar::Function* fun) const {
  auto it = _store.find(fun);
  if (it == _store.end()) return nullptr;
  return &it->second;
}

bool SummaryStore::has(ar::Function* fun) const {
  return _store.count(fun) != 0;
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
