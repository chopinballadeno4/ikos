/*******************************************************************************
 *
 * \file
 * \brief Parameter-symbolic summary integration with the main analysis
 *
 ******************************************************************************/

#pragma once

#include <ikos/ar/semantic/function.hpp>

namespace ikos {
namespace analyzer {

class Context;

/// \brief Build and retain a function summary in the main analysis context
///
/// This does not apply the summary to callers and does not change the value
/// analysis invariant.
void build_and_store_function_summary(Context& ctx, ar::Function* function);

} // end namespace analyzer
} // end namespace ikos
