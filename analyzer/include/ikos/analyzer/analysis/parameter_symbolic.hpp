/*******************************************************************************
 *
 * \file
 * \brief Parameter-symbolic summary integration with the main analysis
 *
 ******************************************************************************/

#pragma once

#include <ikos/ar/semantic/function.hpp>
#include <ikos/ar/semantic/statement.hpp>

#include <ikos/analyzer/analysis/value/abstract_domain.hpp>

namespace ikos {
namespace analyzer {

class Context;

/// \brief Build and retain a function summary in the main analysis context
///
/// This does not apply the summary to callers and does not change the value
/// analysis invariant.
void build_and_store_function_summary(Context& ctx, ar::Function* function);

/// \brief Refine the normal and exception flows using a singleton callee
/// summary
///
/// The input invariant must already contain the result of the existing IKOS
/// call semantics. Unsupported atoms are ignored conservatively.
void apply_caller_summary(Context& ctx,
                          value::AbstractDomain& invariant,
                          ar::CallBase* call,
                          ar::Function* callee);

} // end namespace analyzer
} // end namespace ikos
