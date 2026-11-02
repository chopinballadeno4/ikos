#include <sstream>
#include <stdexcept>
#include <ikos/analyzer/intrinsic.h>
namespace rclcpp {
enum class QosPolicyKind;
std::ostream& operator<<(std::ostream&, const QosPolicyKind&);
namespace detail {
#line 291 "/Users/baesungmin/master/experiment/realworld/rclcpp/rclcpp/include/rclcpp/detail/qos_parameters.hpp"
inline
const char *
check_if_stringified_policy_is_null(const char * policy_value_stringified, QosPolicyKind kind)
{
  if (!policy_value_stringified) {
    std::ostringstream oss{"unknown value for policy kind {", std::ios::ate};
    oss << kind << "}";
    throw std::invalid_argument{oss.str()};
  }
  return policy_value_stringified;
}

}}
#line 1 "rclcpp_fragment_probe.cpp"
extern "C" void probe_normal() {
  const char text[] = "x";
  const char* p = __ikos_nondet_int() ? text : nullptr;
  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());
  (void)rclcpp::detail::check_if_stringified_policy_is_null(p, k);
  __ikos_assert(p != nullptr);
}
extern "C" void probe_return() {
  const char text[] = "x";
  const char* p = __ikos_nondet_int() ? text : nullptr;
  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());
  const char* result = rclcpp::detail::check_if_stringified_policy_is_null(p, k);
  __ikos_assert(result != nullptr);
}
extern "C" void probe_exception() {
  const char text[] = "x";
  const char* p = __ikos_nondet_int() ? text : nullptr;
  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());
  try { (void)rclcpp::detail::check_if_stringified_policy_is_null(p, k); }
  catch (...) { __ikos_assert(p == nullptr); }
}
