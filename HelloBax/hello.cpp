// hello.cpp — actual greeting is composed via std::string + std::ostringstream
// to prove the build is going through a real C++ compiler (cleng.exe on
// the Xbox path; the host's clang++/g++ on the host path).

#include "hello.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace hellobax {

static std::string Compose() {
  std::ostringstream oss;
  oss << "Hello Bax from C++ (" << __cplusplus << ")";
  return oss.str();
}

} // namespace hellobax

extern "C" int hellobax_greet(char *buf, int cap) {
  const std::string greeting = hellobax::Compose();
  if (buf == nullptr || cap <= 0) {
    return static_cast<int>(greeting.size());
  }
  return std::snprintf(buf, static_cast<size_t>(cap), "%s", greeting.c_str());
}
