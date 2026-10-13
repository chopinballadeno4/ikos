/*******************************************************************************
 *
 * \file
 * \brief Parameter-symbolic summary integration with the main analysis
 *
 ******************************************************************************/

#include <sstream>
#include <utility>

#include <ikos/analyzer/analysis/context.hpp>
#include <ikos/analyzer/analysis/parameter_symbolic.hpp>
#include <ikos/analyzer/util/log.hpp>
#include <ikos/frontend/llvm/analysis/parameter_symbolic/symbolic_fixpoint.hpp>

namespace ikos {
namespace analyzer {

void build_and_store_function_summary(Context& ctx, ar::Function* function) {
  if (function == nullptr || !function->is_definition() ||
      ctx.function_summaries.has(function)) {
    return;
  }

  std::ostringstream trace;
  std::ostream* output = ctx.opts.trace_ar_statements ? &trace : nullptr;

  frontend::parameter_symbolic::FunctionSummary summary =
      frontend::parameter_symbolic::analyze_function(function,
                                                      output,
                                                      output,
                                                      function->name());
  ctx.function_summaries.put(function, std::move(summary));

  if (ctx.opts.trace_ar_statements) {
    auto msg = log::msg();
    msg << trace.str();
  }
}

} // end namespace analyzer
} // end namespace ikos
