/*******************************************************************************
 *
 * \file
 * \brief Implementation of SimplifyUpcastComparisonPass
 *
 * Author: Thomas Bailleux
 *
 * Contact: ikos@lists.nasa.gov
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

#include <ikos/ar/pass/simplify_upcast_comparison.hpp>
#include <ikos/ar/semantic/statement.hpp>

namespace ikos {
namespace ar {

const char* SimplifyUpcastComparisonPass::name() const {
  return "simplify-upcast-comparison";
}

const char* SimplifyUpcastComparisonPass::description() const {
  return "Simplify upcast comparison";
}

bool SimplifyUpcastComparisonPass::run_on_code(Code* code) {
  bool change = false;

  for (BasicBlock* bb : *code) {
    if (!bb->empty()) {
      change = this->run_on_statement(bb->front()) || change;
    }
  }

  return change;
}

bool SimplifyUpcastComparisonPass::run_on_statement(Statement* stmt) {
  if (auto cmp = dyn_cast< Comparison >(stmt)) {
    if (cmp->is_integer_predicate()) {
      if (cmp->left()->is_integer_constant() &&
          cmp->right()->is_internal_variable()) {
        return this->run_on_comparison(cmp,
                                       cast< IntegerConstant >(cmp->left()),
                                       cmp->right());
      } else if (cmp->left()->is_internal_variable() &&
                 cmp->right()->is_integer_constant()) {
        return this->run_on_comparison(cmp,
                                       cast< IntegerConstant >(cmp->right()),
                                       cmp->left());
      }
    }
  }

  return false;
}

bool SimplifyUpcastComparisonPass::run_on_comparison(Comparison* cmp,
                                                     IntegerConstant* constant,
                                                     Value* var) {
  BasicBlock* bb = cmp->parent();
  auto it = bb->rend();
  auto et = bb->rend();

  Value* tracked_variable = var;
  bool bitcast = false;           // saw outer bitcast
  bool inner_zext_found = false;  // saw zext after outer bitcast
  UnaryOperation* upcast = nullptr;
  UnaryOperation* saved_bitcast = nullptr;
  UnaryOperation* saved_zext = nullptr;

  while (true) {
    if (it == et) {
      if (bb->num_predecessors() == 1) {
        bb = *bb->predecessor_begin();
        it = bb->rbegin();
        et = bb->rend();
        continue;
      } else {
        if (bitcast && inner_zext_found) {
          upcast = saved_zext;
          break;
        }
        if (bitcast && saved_bitcast) {
          upcast = saved_bitcast;
          break;
        }
        return false;
      }
    }

    auto stmt = *it;

    if (stmt->result_or_null() == tracked_variable) {
      if (!isa< UnaryOperation >(stmt)) {
        if (bitcast && inner_zext_found) {
          upcast = saved_zext;
          break;
        }
        if (bitcast && saved_bitcast) {
          upcast = saved_bitcast;
          break;
        }
        return false;
      }
      upcast = cast< UnaryOperation >(stmt);
      if (!bitcast && upcast->op() == UnaryOperation::Bitcast) {
        // outer bitcast: begin of a cast chain
        saved_bitcast = upcast;
        tracked_variable = upcast->operand();
        upcast = nullptr;
        bitcast = true;
      } else if (!bitcast && (upcast->op() == UnaryOperation::SExt ||
                              upcast->op() == UnaryOperation::ZExt)) {
        break;  // pattern 1 (sext) or 2 (zext)
      } else if (bitcast && !inner_zext_found &&
                 upcast->op() == UnaryOperation::ZExt) {
        // zext after outer bitcast: continue to look for inner bitcast
        // (triple chain: si_N → bitcast → ui_N → zext → ui_M → bitcast → si_M)
        saved_zext = upcast;
        inner_zext_found = true;
        tracked_variable = upcast->operand();
        upcast = nullptr;
      } else if (bitcast && !inner_zext_found &&
                 upcast->op() == UnaryOperation::SExt) {
        break;  // pattern 5 (si → sext → bitcast → ui)
      } else if (bitcast && inner_zext_found &&
                 upcast->op() == UnaryOperation::Bitcast) {
        break;  // triple chain: inner bitcast found (si_N → ui_N)
      } else if (bitcast && inner_zext_found) {
        upcast = saved_zext;  // no inner bitcast; fall back to pattern 3
        break;
      } else {
        return false;
      }
    }

    it++;
  }

  // For bitcast chains with a signed original variable, only less-than
  // predicates are safe. The true branch of y < C (C <= 2^(N-1)-1) guarantees
  // the value is in [0, C-1], so x sige 0 holds. Greater-than predicates allow
  // x < 0 on the true branch (the bit-pattern maps to a large unsigned value).
  auto orig_type = cast< IntegerType >(upcast->operand()->type());
  if (bitcast && orig_type->is_signed()) {
    auto pred = cmp->predicate();
    if (pred != Comparison::UILT && pred != Comparison::UILE &&
        pred != Comparison::SILT && pred != Comparison::SILE) {
      return false;
    }
  }

  // Flip the predicate sign when the comparison type sign differs from the
  // original variable's sign (one bitcast in the chain changes the sign).
  // Two bitcasts cancel out (e.g. triple chain: si → ui → si), so no flip.
  bool pred_is_signed = (cmp->predicate() == Comparison::SILT ||
                         cmp->predicate() == Comparison::SILE ||
                         cmp->predicate() == Comparison::SIGT ||
                         cmp->predicate() == Comparison::SIGE ||
                         cmp->predicate() == Comparison::SIEQ ||
                         cmp->predicate() == Comparison::SINE);
  bool flip_predicate = (pred_is_signed != orig_type->is_signed());
  bool needs_lower_bound = bitcast && orig_type->is_signed();

  if (auto new_constant =
          this->run_on_upcast(cmp->context(), upcast->operand(), constant)) {
    this->insert_assert(cmp,
                        upcast->operand(),
                        constant,
                        *new_constant,
                        flip_predicate,
                        needs_lower_bound);
    return true;
  } else {
    return false;
  }
}

boost::optional< IntegerConstant* > SimplifyUpcastComparisonPass::run_on_upcast(
    Context& context, Value* tracked_variable, IntegerConstant* constant) {
  auto type = cast< IntegerType >(tracked_variable->type());

  ZInterval top =
      IntInterval::top(type->bit_width(), type->sign()).to_z_interval();
  ZNumber zvalue = constant->value().to_z_number();

  // if the constant is superior to the maximum value of the original type
  // (before the cast), then this is an inifinite loop
  if (top.contains(zvalue)) {
    return IntegerConstant::get(context,
                                IntegerType::get(context,
                                                 type->bit_width(),
                                                 type->sign()),
                                constant->value().cast(type->bit_width(),
                                                       type->sign()));
  } else {
    return boost::none;
  }
}

void SimplifyUpcastComparisonPass::insert_assert(Comparison* cmp,
                                                 Value* tracked_variable,
                                                 IntegerConstant* constant,
                                                 IntegerConstant* new_constant,
                                                 bool flip_predicate,
                                                 bool needs_lower_bound) {
  Value* left;
  Value* right;
  if (cmp->left() == constant) {
    left = new_constant;
    right = tracked_variable;
  } else {
    left = tracked_variable;
    right = new_constant;
  }
  Comparison::Predicate predicate = cmp->predicate();
  if (flip_predicate) {
    switch (cmp->predicate()) {
      case Comparison::UIGT: predicate = Comparison::SIGT; break;
      case Comparison::UIGE: predicate = Comparison::SIGE; break;
      case Comparison::UILT: predicate = Comparison::SILT; break;
      case Comparison::UILE: predicate = Comparison::SILE; break;
      case Comparison::SIGT: predicate = Comparison::UIGT; break;
      case Comparison::SIGE: predicate = Comparison::UIGE; break;
      case Comparison::SILT: predicate = Comparison::UILT; break;
      case Comparison::SILE: predicate = Comparison::UILE; break;
      case Comparison::UIEQ: predicate = Comparison::SIEQ; break;
      case Comparison::UINE: predicate = Comparison::SINE; break;
      case Comparison::SIEQ: predicate = Comparison::UIEQ; break;
      case Comparison::SINE: predicate = Comparison::UINE; break;
      default: ikos_unreachable("unreachable");
    }
  }
  auto c = Comparison::create(predicate, left, right);
  auto bb = cmp->parent();
  auto it = bb->begin();
  bb->insert_after(it, std::move(c));

  if (needs_lower_bound) {
    auto type = cast< IntegerType >(tracked_variable->type());
    auto zero = IntegerConstant::get(cmp->context(),
                                     type,
                                     MachineInt(0,
                                                type->bit_width(),
                                                type->sign()));
    auto zero_cmp = Comparison::create(Comparison::SIGE, tracked_variable, zero);
    auto it2 = bb->begin();
    bb->insert_after(it2, std::move(zero_cmp));
  }
}

} // end namespace ar
} // end namespace ikos
