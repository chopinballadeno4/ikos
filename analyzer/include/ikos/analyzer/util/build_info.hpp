#pragma once

namespace ikos {
namespace analyzer {

// Generated at build time and linked into the analyzer actually being run.
const char* build_info_json();
const char* build_version_banner();

} // namespace analyzer
} // namespace ikos
