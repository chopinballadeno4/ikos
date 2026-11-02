#include <tinyxml2.h>
#include <ikos/analyzer/intrinsic.h>

extern "C" void probe_normal() {
  int choose = __ikos_nondet_int();
  const char text[] = "abc";
  const char* p = choose ? text : nullptr;
  bool bom;
  (void)tinyxml2::XMLUtil::ReadBOM(p, &bom);
  __ikos_assert(p != nullptr);
}

extern "C" void probe_set() {
  int choose = __ikos_nondet_int();
  const char text[] = "abc";
  const char* p = choose ? text : nullptr;
  tinyxml2::StrPair pair;
  pair.SetStr(p);
  __ikos_assert(p != nullptr);
}

extern "C" void probe_return() {
  int choose = __ikos_nondet_int();
  const char text[] = "abc";
  const char* p = choose ? text : nullptr;
  bool bom;
  const char* result = tinyxml2::XMLUtil::ReadBOM(p, &bom);
  __ikos_assert(result != nullptr);
}
