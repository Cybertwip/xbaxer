// hello.cpp — minimal C++ implementation that intentionally avoids the
// standard library so cleng can compile it for any GOOS/GOARCH without
// access to a platform SDK. The arithmetic still goes through a C++
// template to prove cleng is in C++ mode (a C compiler would reject the
// `template` keyword).

#include "hello.h"

namespace hellobax {

template <typename T>
static T Add(T a, T b) {
  return a + b;
}

} // namespace hellobax

extern "C" int hellobax_sum(int a, int b) {
  return hellobax::Add<int>(a, b);
}
