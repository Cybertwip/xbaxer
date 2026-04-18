// hello.h — C-linkage facade so cgo can call the C++ implementation in
// hello.cpp. cgo doesn't understand C++ name mangling; everything that
// crosses the Go ↔ C++ boundary has to be declared `extern "C"`.

#ifndef HELLOBAX_HELLO_H
#define HELLOBAX_HELLO_H

#ifdef __cplusplus
extern "C" {
#endif

// hellobax_greet writes a NUL-terminated greeting (composed in C++) into
// the caller-provided buffer and returns the number of bytes that would
// have been written excluding the terminator (snprintf semantics). If
// `buf` is NULL or `cap` is 0 nothing is written.
int hellobax_greet(char *buf, int cap);

#ifdef __cplusplus
}
#endif

#endif
