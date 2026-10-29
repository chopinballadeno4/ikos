/*******************************************************************************
 *
 * Tests for flow-local expression bounds
 *
 * Notices:
 *
 * Copyright (c) 2018-2019 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Disclaimers:
 *
 * No Warranty: THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF
 * ANY KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO SPECIFICATIONS,
 * ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL BE
 * ERROR FREE, OR ANY WARRANTY THAT DOCUMENTATION, IF PROVIDED, WILL CONFORM TO
 * THE SUBJECT SOFTWARE. THIS AGREEMENT DOES NOT, IN ANY MANNER, CONSTITUTE AN
 * ENDORSEMENT BY GOVERNMENT AGENCY OR ANY PRIOR RECIPIENT OF ANY RESULTS,
 * RESULTING DESIGNS, HARDWARE, SOFTWARE PRODUCTS OR ANY OTHER APPLICATIONS
 * RESULTING FROM USE OF THE SUBJECT SOFTWARE.  FURTHER, GOVERNMENT AGENCY
 * DISCLAIMS ALL WARRANTIES AND LIABILITIES REGARDING THIRD-PARTY SOFTWARE,
 * IF PRESENT IN THE ORIGINAL SOFTWARE, AND DISTRIBUTES IT "AS IS."
 *
 * Waiver and Indemnity:  RECIPIENT AGREES TO WAIVE ANY AND ALL CLAIMS AGAINST
 * THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS, AS WELL
 * AS ANY PRIOR RECIPIENT.  IF RECIPIENT'S USE OF THE SUBJECT SOFTWARE RESULTS
 * IN ANY LIABILITIES, DEMANDS, DAMAGES, EXPENSES OR LOSSES ARISING FROM SUCH
 * USE, INCLUDING ANY DAMAGES FROM PRODUCTS BASED ON, OR RESULTING FROM,
 * RECIPIENT'S USE OF THE SUBJECT SOFTWARE, RECIPIENT SHALL INDEMNIFY AND HOLD
 * HARMLESS THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS,
 * AS WELL AS ANY PRIOR RECIPIENT, TO THE EXTENT PERMITTED BY LAW.
 * RECIPIENT'S SOLE REMEDY FOR ANY SUCH MATTER SHALL BE THE IMMEDIATE,
 * UNILATERAL TERMINATION OF THIS AGREEMENT.
 *
 ******************************************************************************/

#define BOOST_TEST_MODULE test_memory_expression_ranges
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <ikos/core/domain/exception/exception.hpp>
#include <ikos/core/domain/machine_int/interval.hpp>
#include <ikos/core/domain/memory/dummy.hpp>
#include <ikos/core/domain/memory/polymorphic_domain.hpp>
#include <ikos/core/domain/scalar/machine_int.hpp>
#include <ikos/core/domain/uninitialized/separate_domain.hpp>
#include <ikos/core/example/memory_factory.hpp>
#include <ikos/core/example/scalar/variable_factory.hpp>

using namespace ikos::core;
using Int = MachineInt;
using Interval = machine_int::Interval;
using B = machine_int::BinaryOperator;
using P = machine_int::Predicate;
using Factory = example::scalar::VariableFactory;
using V = Factory::VariableRef;
using M = example::MemoryFactory::MemoryLocationRef;
using U = uninitialized::SeparateDomain< V >;
using N = machine_int::IntervalDomain< V >;
using S = scalar::MachineIntDomain< V, M, U, N >;
using D = memory::DummyDomain< V, M, S >;
using State = memory::PolymorphicDomain< V, M >;

static State top() {
  State s(D(S(U::top(), N::top())));
  s.enable_expression_ranges(
      [](V v) { return scalar::VariableTraits< V >::is_int(v); });
  return s;
}
static void guard(State& s, V a, V b, V t) {
  s.int_apply(B::Add, t, a, b);
  s.int_add(P::NE, t, Int(0, 32, Unsigned));
  s.int_add(P::LE, t, Int(10000, 32, Unsigned));
}
static bool contains_zero(const Interval& i) {
  return !i.is_bottom() && i.lb().to_z_number() <= 0 &&
         i.ub().to_z_number() >= 0;
}

BOOST_AUTO_TEST_CASE(reuse_after_temporary_forget_and_copy) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned), r = f.get_int("r", 32, Unsigned);
  auto s = top();
  guard(s, a, b, t);
  s.scalar_forget(t);
  State copy = s;
  copy.int_apply(B::Add, r, a, b);
  BOOST_CHECK(copy.int_to_interval(r).lb() == Int(1, 32, Unsigned));
  BOOST_CHECK(copy.int_to_interval(r).ub() == Int(10000, 32, Unsigned));
}
BOOST_AUTO_TEST_CASE(unguarded_join_and_widen_keep_zero_possible) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned), r = f.get_int("r", 32, Unsigned);
  auto guarded = top();
  guard(guarded, a, b, t);
  guarded.scalar_forget(t);
  auto base = top();
  BOOST_CHECK(guarded.leq(base));
  BOOST_CHECK(!base.leq(guarded));
  auto joined = guarded.join(base);
  joined.int_apply(B::Add, r, a, b);
  BOOST_CHECK(contains_zero(joined.int_to_interval(r)));
  auto widened = guarded.widening(base);
  widened.int_apply(B::Add, r, a, b);
  BOOST_CHECK(contains_zero(widened.int_to_interval(r)));
}
BOOST_AUTO_TEST_CASE(redefinition_invalidates_transitive_identity) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned), r = f.get_int("r", 32, Unsigned);
  auto s = top();
  guard(s, a, b, t);
  s.scalar_forget(t);
  s.scalar_assign_nondet(a);
  s.int_apply(B::Add, r, a, b);
  BOOST_CHECK(contains_zero(s.int_to_interval(r)));
}
BOOST_AUTO_TEST_CASE(distinct_unknown_values_do_not_share_an_expression) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto c = f.get_int("c", 32, Unsigned), t = f.get_int("t", 32, Unsigned),
       r = f.get_int("r", 32, Unsigned);
  auto s = top();
  guard(s, a, b, t);
  s.scalar_forget(t);
  s.int_apply(B::Add, r, a, c);
  BOOST_CHECK(contains_zero(s.int_to_interval(r)));
}
BOOST_AUTO_TEST_CASE(wrapping_and_operand_order) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned), r = f.get_int("r", 32, Unsigned);
  auto s = top();
  guard(s, a, b, t);
  s.scalar_forget(t);
  s.int_apply(B::Add, r, b, a);
  BOOST_CHECK(!contains_zero(s.int_to_interval(r)));
  s.int_assign(a, Int::max(32, Unsigned));
  s.int_assign(b, Int(2, 32, Unsigned));
  s.int_apply(B::Add, r, a, b);
  BOOST_CHECK(s.int_to_interval(r).singleton() == Int(1, 32, Unsigned));
}
BOOST_AUTO_TEST_CASE(signed_nonzero_hole_is_not_invented) {
  Factory f;
  auto a = f.get_int("a", 32, Signed), b = f.get_int("b", 32, Signed);
  auto t = f.get_int("t", 32, Signed), r = f.get_int("r", 32, Signed);
  auto s = top();
  s.int_apply(B::Add, t, a, b);
  s.int_add(P::NE, t, Int(0, 32, Signed));
  s.scalar_forget(t);
  s.int_apply(B::Add, r, a, b);
  BOOST_CHECK(contains_zero(s.int_to_interval(r)));
}
BOOST_AUTO_TEST_CASE(exception_flow_does_not_inherit_normal_bounds) {
  Factory f;
  auto a = f.get_int("a", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned), r = f.get_int("r", 32, Unsigned);
  auto normal = top();
  guard(normal, a, b, t);
  normal.scalar_forget(t);
  auto empty = top();
  empty.set_to_bottom();
  exception::ExceptionDomain< State > state(normal, top(), empty);
  state.enter_catch();
  state.normal().int_apply(B::Add, r, a, b);
  BOOST_CHECK(contains_zero(state.normal().int_to_interval(r)));
}

BOOST_AUTO_TEST_CASE(
    backward_offset_reaches_original_variable_through_sign_cast) {
  Factory f;
  auto raw = f.get_int("raw", 32, Signed);
  auto n = f.get_int("n", 32, Unsigned), t = f.get_int("t", 32, Unsigned);
  auto s = top();
  s.int_apply(machine_int::UnaryOperator::SignCast, n, raw);
  s.int_apply(B::Sub, t, n, Int(1, 32, Unsigned));
  s.int_add(P::GE, t, Int(1, 32, Unsigned));
  s.int_add(P::LE, t, Int(1000, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).lb() == Int(2, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).ub() == Int(1001, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(raw).lb() == Int(2, 32, Signed));
  BOOST_CHECK(s.int_to_interval(raw).ub() == Int(1001, 32, Signed));

  // Summary projection must also work after the argument temporary dies,
  // independently of any refinement performed while inlining the callee.
  s = top();
  s.int_apply(B::Sub, t, n, Int(1, 32, Unsigned));
  auto argument = s.range_expression(t);
  s.scalar_forget(t);
  s.refine_expression(argument,
                      Interval(Int(1, 32, Unsigned), Int(1000, 32, Unsigned)));
  BOOST_CHECK(s.int_to_interval(n).lb() == Int(2, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).ub() == Int(1001, 32, Unsigned));
}

BOOST_AUTO_TEST_CASE(backward_wrap_must_keep_zero_and_large_operands) {
  Factory f;
  auto n = f.get_int("n", 32, Unsigned), t = f.get_int("t", 32, Unsigned);
  auto s = top();
  s.int_apply(B::Add, t, n, Int(1, 32, Unsigned));
  s.int_add(P::LE, t, Int(1, 32, Unsigned));
  BOOST_CHECK(contains_zero(s.int_to_interval(n)));
  BOOST_CHECK(s.int_to_interval(n).contains(Int::max(32, Unsigned)));

  s = top();
  s.int_apply(B::Mul, t, n, Int(2, 32, Unsigned));
  s.int_add(P::EQ, t, Int(2, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).contains(Int(1, 32, Unsigned)));
  BOOST_CHECK(s.int_to_interval(n).contains(Int(2147483649ULL, 32, Unsigned)));
}

BOOST_AUTO_TEST_CASE(backward_division_and_bounded_variable_subtraction) {
  Factory f;
  auto n = f.get_int("n", 32, Unsigned), b = f.get_int("b", 32, Unsigned);
  auto t = f.get_int("t", 32, Unsigned);
  auto s = top();
  s.int_apply(B::Div, t, n, Int(2, 32, Unsigned));
  s.int_add(P::GE, t, Int(1, 32, Unsigned));
  s.int_add(P::LE, t, Int(3, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).lb() == Int(2, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).ub() == Int(7, 32, Unsigned));

  s = top();
  s.int_set(b, Interval(Int(1, 32, Unsigned), Int(3, 32, Unsigned)));
  s.int_apply(B::Sub, t, n, b);
  s.int_add(P::GE, t, Int(1, 32, Unsigned));
  s.int_add(P::LE, t, Int(10, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).lb() == Int(2, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).ub() == Int(13, 32, Unsigned));
}

BOOST_AUTO_TEST_CASE(truncation_and_remainder_do_not_invent_operand_bounds) {
  Factory f;
  auto n = f.get_int("n", 32, Unsigned), narrow = f.get_int("low", 8, Unsigned);
  auto t = f.get_int("t", 32, Unsigned);
  auto s = top();
  s.int_apply(machine_int::UnaryOperator::Trunc, narrow, n);
  s.int_add(P::EQ, narrow, Int(1, 8, Unsigned));
  BOOST_CHECK(contains_zero(s.int_to_interval(n)));
  BOOST_CHECK(s.int_to_interval(n).contains(Int(257, 32, Unsigned)));

  s = top();
  s.int_apply(B::Rem, t, n, Int(2, 32, Unsigned));
  s.int_add(P::EQ, t, Int(0, 32, Unsigned));
  BOOST_CHECK(s.int_to_interval(n).is_top());
}

// Exhaustive concrete semantics over small machine types: every input that
// satisfies a result interval must survive the projected operand intervals.
BOOST_AUTO_TEST_CASE(backward_arithmetic_contains_all_concrete_solutions) {
  std::size_t solutions = 0;
  for (auto sign : {Signed, Unsigned}) {
    int min = sign == Signed ? -4 : 0, max = sign == Signed ? 3 : 7;
    auto interval = [&](int lo, int hi) {
      return Interval(Int(lo, 3, sign), Int(hi, 3, sign));
    };
    for (auto op : {B::Add,
                    B::Sub,
                    B::AddNoWrap,
                    B::SubNoWrap,
                    B::Mul,
                    B::MulNoWrap,
                    B::Div,
                    B::DivExact}) {
      for (int k = min; k <= max; ++k) {
        for (int bound = 0; bound < 2; ++bound) {
          int first = bound ? std::max(min, -1) : min;
          int last = bound ? std::min(max, 2) : max;
          auto left = interval(first, last), right = interval(k, k);
          for (int lo = min; lo <= max; ++lo) {
            for (int hi = lo; hi <= max; ++hi) {
              auto result = interval(lo, hi);
              auto projected =
                  machine_int::backward_intervals(op, result, left, right);
              for (int x = first; x <= last; ++x) {
                int value;
                if (op == B::Add || op == B::AddNoWrap)
                  value = x + k;
                else if (op == B::Sub || op == B::SubNoWrap)
                  value = x - k;
                else if (op == B::Mul || op == B::MulNoWrap)
                  value = x * k;
                else {
                  if (k == 0 || (sign == Signed && x == min && k == -1))
                    continue;
                  if (op == B::DivExact && x % k != 0)
                    continue;
                  value = x / k;
                }
                if ((op == B::AddNoWrap || op == B::SubNoWrap ||
                     op == B::MulNoWrap) &&
                    (value < min || value > max))
                  continue;
                if (result.contains(Int(value, 3, sign))) {
                  ++solutions;
                  if (!projected.first.contains(Int(x, 3, sign)) ||
                      !projected.second.contains(Int(k, 3, sign)))
                    BOOST_FAIL(
                        "backward arithmetic discarded a concrete solution");
                }
              }
            }
          }
        }
      }
    }
    for (auto op : {B::Add, B::Sub}) {
      auto left = interval(min, max), right = interval(0, 2);
      for (int lo = min; lo <= max; ++lo) {
        for (int hi = lo; hi <= max; ++hi) {
          auto result = interval(lo, hi);
          auto projected =
              machine_int::backward_intervals(op, result, left, right);
          for (int x = min; x <= max; ++x) {
            for (int y = 0; y <= 2; ++y) {
              Int value(op == B::Add ? x + y : x - y, 3, sign);
              if (result.contains(value)) {
                ++solutions;
                if (!projected.first.contains(Int(x, 3, sign)) ||
                    !projected.second.contains(Int(y, 3, sign)))
                  BOOST_FAIL(
                      "variable arithmetic discarded a concrete solution");
              }
            }
          }
        }
      }
    }
  }
  BOOST_CHECK(solutions > 0);
}
