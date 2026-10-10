#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_expr.hpp>

#include <ostream>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

void SymbolicExpr::dump(std::ostream& o,
                        const std::vector<std::string>& param_names) const {
  bool first = true;
  for (auto it2 = coeffs.begin(); it2 != coeffs.end(); ++it2) {
    int idx = it2->first;
    int c   = it2->second;
    if (c == 0) continue;
    if (!first) o << (c > 0 ? " + " : " - ");
    else if (c < 0) o << "-";
    int ac = (c < 0 ? -c : c);
    if (ac != 1) o << ac << "*";
    const std::string& name =
        (idx < (int)param_names.size()) ? param_names[idx]
                                        : ("p" + std::to_string(idx));
    o << name << "0";
    first = false;
  }
  if (constant != 0 || first) {
    if (!first) o << (constant >= 0 ? " + " : " - ");
    else if (constant < 0) o << "-";
    o << (constant < 0 ? -constant : constant);
  }
}

void dump_sym_val(std::ostream& o,
                  const SymbolicValue& v,
                  const std::vector<std::string>& param_names) {
  if (v.is_top()) {
    o << "TOP";
  } else if (const SymbolicExpr* e = v.get_expr()) {
    e->dump(o, param_names);
  } else if (const SymbolicLoc* l = v.get_loc()) {
    dump_sym_loc(o, *l, param_names);
  }
}

void dump_sym_loc(std::ostream& o,
                  const SymbolicLoc& loc,
                  const std::vector<std::string>& param_names) {
  bool zero_offset = loc.offset.is_const() && loc.offset.constant == 0;
  if (zero_offset) {
    o << "L[";
    if (loc.base && loc.base->has_name()) o << loc.base->name();
    else o << "?";
    o << "]";
  } else {
    o << "Loc(L[";
    if (loc.base && loc.base->has_name()) o << loc.base->name();
    else o << "?";
    o << "], ";
    loc.offset.dump(o, param_names);
    o << ")";
  }
  if (!loc.callee_local) o << "(ext)";
}

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
