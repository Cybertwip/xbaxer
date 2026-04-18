// hello.h — C-linkage facade so cgo can call the C++ implementation in
// hello.cpp without needing the platform's libc/libc++ headers.
//
// We deliberately use only built-in types here (`int`) so this header
// pulls in zero standard library headers — clang would otherwise look
// for `<stddef.h>` etc. via the system SDK, which sarver-on-Xbox does
// not have for non-Windows targets.

#ifndef HELLOBAX_HELLO_H
#define HELLOBAX_HELLO_H

#ifdef __cplusplus
extern "C" {
#endif

// hellobax_sum returns the sum of two integers, computed in C++.
int hellobax_sum(int a, int b);

#ifdef __cplusplus
}
#endif

#endif
