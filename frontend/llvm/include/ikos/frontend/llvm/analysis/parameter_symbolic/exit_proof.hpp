#pragma once

#include <ikos/frontend/llvm/analysis/parameter_symbolic/function_summary.hpp>

namespace ikos {
namespace frontend {
namespace parameter_symbolic {

/// Build certified sufficient exit conditions. Finite path expansion is used
/// only for condition extraction; sigma/mu remain the original flat domain.
/// An unsupported path contributes no guaranteed inputs and makes coverage
/// Partial. Normal coverage can remain complete when every CFG continuation
/// of the unsupported path is proved unable to return normally. Exception
/// coverage remains partial. A missing guarantee alone never excludes a flow.
FunctionSummary prove_exit_conditions(ar::Function* function,
                                      const SummaryStore* store);

} // namespace parameter_symbolic
} // namespace frontend
} // namespace ikos
