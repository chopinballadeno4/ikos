#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <utility>

#include <ikos/core/domain/machine_int/operator.hpp>
#include <ikos/core/value/machine_int/interval.hpp>

namespace ikos {
namespace core {
namespace machine_int {

/// Sound one-interval projection of an integer comparison. Interior holes
/// deliberately remain unrepresented; this is not a non-zero domain.
inline Interval assume_interval(Interval i, Predicate p, const MachineInt& n) {
  if (i.is_bottom())
    return i;
  ZNumber lo = i.lb().to_z_number(), hi = i.ub().to_z_number(),
          c = n.to_z_number();
  switch (p) {
    case Predicate::EQ:
      lo = std::max(lo, c);
      hi = std::min(hi, c);
      break;
    case Predicate::NE:
      if (lo == c)
        ++lo;
      if (hi == c)
        --hi;
      break;
    case Predicate::GE:
      lo = std::max(lo, c);
      break;
    case Predicate::GT:
      lo = std::max(lo, ZNumber(c + 1));
      break;
    case Predicate::LE:
      hi = std::min(hi, c);
      break;
    case Predicate::LT:
      hi = std::min(hi, ZNumber(c - 1));
      break;
  }
  if (lo > hi)
    return Interval::bottom(i.bit_width(), i.sign());
  return Interval(MachineInt(lo, i.bit_width(), i.sign()),
                  MachineInt(hi, i.bit_width(), i.sign()));
}

/// Intersect with mathematical bounds, clipping before machine conversion.
inline Interval restrict_interval(const Interval& i, ZNumber lo, ZNumber hi) {
  if (i.is_bottom())
    return i;
  lo = std::max(lo, i.lb().to_z_number());
  hi = std::min(hi, i.ub().to_z_number());
  if (lo > hi)
    return Interval::bottom(i.bit_width(), i.sign());
  return Interval(MachineInt(lo, i.bit_width(), i.sign()),
                  MachineInt(hi, i.bit_width(), i.sign()));
}

/// Sound projections of result constraints onto arithmetic operands. These
/// are necessary conditions, never sufficient exit conditions. Unsupported
/// inverses keep both input ranges. In particular modular multiplication must
/// not be inverted using ordinary integer division.
inline std::pair< Interval, Interval > backward_intervals(
    BinaryOperator op,
    const Interval& result,
    const Interval& left,
    const Interval& right) {
  if (result.is_bottom() || left.is_bottom() || right.is_bottom())
    return {Interval::bottom(left.bit_width(), left.sign()),
            Interval::bottom(right.bit_width(), right.sign())};
  switch (op) {
    case BinaryOperator::Add:
    case BinaryOperator::AddNoWrap:
      return {left.meet(sub(result, right)), right.meet(sub(result, left))};
    case BinaryOperator::Sub:
    case BinaryOperator::SubNoWrap:
      return {left.meet(add(result, right)), right.meet(sub(left, result))};
    case BinaryOperator::Mul:
    case BinaryOperator::MulNoWrap: {
      auto product = left.to_z_interval() * right.to_z_interval();
      bool no_wrap =
          op == BinaryOperator::MulNoWrap ||
          (*product.lb().number() >=
               MachineInt::min(left.bit_width(), left.sign()).to_z_number() &&
           *product.ub().number() <=
               MachineInt::max(left.bit_width(), left.sign()).to_z_number());
      auto inverse = [&](const Interval& operand, const Interval& factor) {
        auto constant = factor.singleton();
        if (!constant || constant->is_zero())
          return operand;
        ZNumber k = constant->to_z_number();
        if (k == 1)
          return operand.meet(result);
        if (!no_wrap)
          return operand;
        ZNumber lo = result.lb().to_z_number();
        ZNumber hi = result.ub().to_z_number();
        if (k < 0) {
          k = -k;
          ZNumber old_lo = lo;
          lo = -hi;
          hi = -old_lo;
        }
        // ZNumber division truncates towards zero; adjust to ceil/floor.
        ZNumber lower = lo / k, upper = hi / k;
        if (lo > 0 && lo % k != 0)
          ++lower;
        if (hi < 0 && hi % k != 0)
          --upper;
        return restrict_interval(operand, lower, upper);
      };
      return {inverse(left, right), inverse(right, left)};
    }
    case BinaryOperator::Div:
    case BinaryOperator::DivExact: {
      auto constant = right.singleton();
      if (!constant || constant->to_z_number() <= 0)
        return {left, right};
      ZNumber k = constant->to_z_number();
      ZNumber lo = result.lb().to_z_number();
      ZNumber hi = result.ub().to_z_number();
      ZNumber lower = lo * k, upper = hi * k;
      if (lo <= 0)
        lower -= k - 1;
      if (hi >= 0)
        upper += k - 1;
      return {restrict_interval(left, lower, upper), right};
    }
    default:
      return {left, right};
  }
}

/// Immutable, typed computation identity. Unknown values have distinct leaves;
/// an unknown expression is never an identity shared by unrelated values.
template < typename VariableRef >
class Expression {
public:
  enum Kind { Constant, Variable, Binary, Cast };
  struct Node {
    Kind kind;
    uint64_t width;
    Signedness sign;
    VariableRef variable{};
    MachineInt constant = MachineInt(0, 1, Unsigned);
    BinaryOperator op = BinaryOperator::Add;
    std::shared_ptr< const Node > left, right;
    unsigned depth = 1;
  };

private:
  std::shared_ptr< const Node > _node;
  explicit Expression(std::shared_ptr< const Node > n) : _node(std::move(n)) {}

public:
  Expression() = default;
  explicit operator bool() const { return bool(_node); }
  const Node& node() const { return *_node; }
  uint64_t bit_width() const { return _node->width; }
  Signedness sign() const { return _node->sign; }
  bool is_constant() const { return _node && _node->kind == Constant; }
  bool is_variable() const { return _node && _node->kind == Variable; }
  Expression left() const { return Expression(_node->left); }
  Expression right() const { return Expression(_node->right); }

  static Expression constant(const MachineInt& c) {
    auto n = std::make_shared< Node >();
    n->kind = Constant;
    n->width = c.bit_width();
    n->sign = c.sign();
    n->constant = c;
    return Expression(n);
  }
  static Expression variable(VariableRef v, uint64_t w, Signedness s) {
    auto n = std::make_shared< Node >();
    n->kind = Variable;
    n->width = w;
    n->sign = s;
    n->variable = v;
    return Expression(n);
  }
  static Expression cast(Expression e, uint64_t w, Signedness s) {
    if (!e || e.node().depth >= 32)
      return {};
    if (e.bit_width() == w && e.sign() == s)
      return e;
    if (e.is_constant())
      return constant(e.node().constant.cast(w, s));
    auto n = std::make_shared< Node >();
    n->kind = Cast;
    n->width = w;
    n->sign = s;
    n->left = e._node;
    n->depth = e.node().depth + 1;
    return Expression(n);
  }
  static Expression binary(BinaryOperator op, Expression l, Expression r) {
    if (!l || !r || l.bit_width() != r.bit_width() || l.sign() != r.sign() ||
        std::max(l.node().depth, r.node().depth) >= 32)
      return {};
    // Only genuinely commutative operations with identical typed semantics.
    if ((op == BinaryOperator::Add || op == BinaryOperator::AddNoWrap ||
         op == BinaryOperator::Mul || op == BinaryOperator::MulNoWrap ||
         op == BinaryOperator::And || op == BinaryOperator::Or ||
         op == BinaryOperator::Xor) &&
        r < l)
      std::swap(l, r);
    auto n = std::make_shared< Node >();
    n->kind = Binary;
    n->width = l.bit_width();
    n->sign = l.sign();
    n->op = op;
    n->left = l._node;
    n->right = r._node;
    n->depth = std::max(l.node().depth, r.node().depth) + 1;
    Expression result(n);
    if (l.is_constant() && r.is_constant()) {
      auto i = result.evaluate([](VariableRef, uint64_t w, Signedness s) {
        return Interval::top(w, s);
      });
      if (auto c = i.singleton())
        return constant(*c);
    }
    return result;
  }
  bool depends_on(VariableRef v) const {
    if (!_node)
      return false;
    if (_node->kind == Variable)
      return _node->variable == v;
    return (_node->left && left().depends_on(v)) ||
           (_node->right && right().depends_on(v));
  }
  bool operator<(const Expression& o) const {
    if (!_node || !o._node)
      return !_node && bool(o._node);
    if (_node->kind != o._node->kind)
      return _node->kind < o._node->kind;
    if (bit_width() != o.bit_width())
      return bit_width() < o.bit_width();
    if (sign() != o.sign())
      return sign() < o.sign();
    if (_node->kind == Constant)
      return _node->constant < o._node->constant;
    if (_node->kind == Variable)
      return std::less< VariableRef >{}(_node->variable, o._node->variable);
    if (_node->op != o._node->op)
      return _node->op < o._node->op;
    if (left() != o.left())
      return left() < o.left();
    return right() < o.right();
  }
  bool operator==(const Expression& o) const {
    return !(*this < o) && !(o < *this);
  }
  bool operator!=(const Expression& o) const { return !(*this == o); }

  template < typename LeafRange >
  Interval evaluate(LeafRange range) const {
    const auto& n = node();
    if (n.kind == Constant)
      return Interval(n.constant);
    if (n.kind == Variable)
      return range(n.variable, n.width, n.sign);
    Interval l = left().evaluate(range);
    if (n.kind == Cast)
      return l.cast(n.width, n.sign);
    Interval r = right().evaluate(range);
    switch (n.op) {
      case BinaryOperator::Add:
        return add(l, r);
      case BinaryOperator::AddNoWrap:
        return add_no_wrap(l, r);
      case BinaryOperator::Sub:
        return sub(l, r);
      case BinaryOperator::SubNoWrap:
        return sub_no_wrap(l, r);
      case BinaryOperator::Mul:
        return mul(l, r);
      case BinaryOperator::MulNoWrap:
        return mul_no_wrap(l, r);
      case BinaryOperator::Div:
        return div(l, r);
      case BinaryOperator::DivExact:
        return div_exact(l, r);
      case BinaryOperator::Rem:
        return rem(l, r);
      case BinaryOperator::Shl:
        return shl(l, r);
      case BinaryOperator::ShlNoWrap:
        return shl_no_wrap(l, r);
      case BinaryOperator::LShr:
        return lshr(l, r);
      case BinaryOperator::LShrExact:
        return lshr_exact(l, r);
      case BinaryOperator::AShr:
        return ashr(l, r);
      case BinaryOperator::AShrExact:
        return ashr_exact(l, r);
      case BinaryOperator::And:
        return and_(l, r);
      case BinaryOperator::Or:
        return or_(l, r);
      case BinaryOperator::Xor:
        return xor_(l, r);
    }
    return Interval::top(n.width, n.sign);
  }

  template < typename LeafSubstitution >
  Expression substitute(LeafSubstitution subst) const {
    if (!_node)
      return {};
    if (node().kind == Constant)
      return *this;
    if (node().kind == Variable)
      return subst(node().variable, bit_width(), sign());
    auto l = left().substitute(subst);
    if (node().kind == Cast)
      return cast(l, bit_width(), sign());
    return binary(node().op, l, right().substitute(subst));
  }

  template < typename DumpLeaf >
  void dump(std::ostream& o, DumpLeaf dump_leaf) const {
    if (!_node) {
      o << "unknown";
      return;
    }
    if (node().kind == Constant) {
      o << node().constant.to_z_number();
      return;
    }
    if (node().kind == Variable) {
      dump_leaf(o, node().variable);
      return;
    }
    if (node().kind == Cast) {
      o << (sign() == Signed ? "SCast_" : "UCast_") << bit_width() << "(";
      left().dump(o, dump_leaf);
      o << ")";
      return;
    }
    const char* name = "Op";
    switch (node().op) {
      case BinaryOperator::Add:
      case BinaryOperator::AddNoWrap:
        name = "Add";
        break;
      case BinaryOperator::Sub:
      case BinaryOperator::SubNoWrap:
        name = "Sub";
        break;
      case BinaryOperator::Mul:
      case BinaryOperator::MulNoWrap:
        name = "Mul";
        break;
      case BinaryOperator::Div:
      case BinaryOperator::DivExact:
        name = "Div";
        break;
      case BinaryOperator::Rem:
        name = "Rem";
        break;
      case BinaryOperator::Shl:
      case BinaryOperator::ShlNoWrap:
        name = "Shl";
        break;
      case BinaryOperator::LShr:
      case BinaryOperator::LShrExact:
        name = "LShr";
        break;
      case BinaryOperator::AShr:
      case BinaryOperator::AShrExact:
        name = "AShr";
        break;
      case BinaryOperator::And:
        name = "And";
        break;
      case BinaryOperator::Or:
        name = "Or";
        break;
      case BinaryOperator::Xor:
        name = "Xor";
        break;
    }
    o << (sign() == Signed ? 'S' : 'U') << name << "_" << bit_width() << "(";
    left().dump(o, dump_leaf);
    o << ", ";
    right().dump(o, dump_leaf);
    o << ")";
  }
};

/// Flow-local expression bounds. Missing bounds are TOP. Bindings are exact
/// value identities, not path predicates, and are kept only when equal at join.
template < typename VariableRef >
class ExpressionRanges {
public:
  using Expr = Expression< VariableRef >;
  std::map< VariableRef, Expr > bindings;
  std::map< Expr, Interval > bounds;

  Expr key(VariableRef v, uint64_t w, Signedness s) const {
    auto it = bindings.find(v);
    return it == bindings.end() ? Expr::variable(v, w, s) : it->second;
  }
  void clear() {
    bindings.clear();
    bounds.clear();
  }
  void redefine(VariableRef v) {
    for (auto it = bindings.begin(); it != bindings.end();)
      if (it->first == v || it->second.depends_on(v))
        it = bindings.erase(it);
      else
        ++it;
    for (auto it = bounds.begin(); it != bounds.end();)
      if (it->first.depends_on(v))
        it = bounds.erase(it);
      else
        ++it;
  }
  // Forgetting a dead temporary is not a write to its operand values.
  void forget(VariableRef v) { bindings.erase(v); }
  Interval lookup(const Expr& e) const {
    auto it = bounds.find(e);
    return it == bounds.end() ? Interval::top(e.bit_width(), e.sign())
                              : it->second;
  }
  void record(const Expr& e, const Interval& i) {
    if (!e)
      return;
    auto it = bounds.find(e);
    if (it != bounds.end())
      it->second.meet_with(i);
    else if (!i.is_top() && bounds.size() < 256)
      bounds.emplace(e, i);
  }
  void bind(VariableRef v, const Expr& e) {
    if (e && !e.depends_on(v) && bindings.size() < 256)
      bindings[v] = e;
  }
  bool leq(const ExpressionRanges& o) const {
    for (const auto& kv : o.bindings) {
      auto it = bindings.find(kv.first);
      if (it == bindings.end() || it->second != kv.second)
        return false;
    }
    for (const auto& kv : o.bounds)
      if (!lookup(kv.first).leq(kv.second))
        return false;
    return true;
  }
  void join_with(const ExpressionRanges& o, bool widen = false) {
    for (auto it = bindings.begin(); it != bindings.end();) {
      auto other = o.bindings.find(it->first);
      if (other == o.bindings.end() || it->second != other->second)
        it = bindings.erase(it);
      else
        ++it;
    }
    for (auto it = bounds.begin(); it != bounds.end();) {
      auto other = o.bounds.find(it->first);
      if (other == o.bounds.end())
        it = bounds.erase(it);
      else {
        if (widen)
          it->second.widen_with(other->second);
        else
          it->second.join_with(other->second);
        if (it->second.is_top())
          it = bounds.erase(it);
        else
          ++it;
      }
    }
  }
};

} // namespace machine_int
} // namespace core
} // namespace ikos
