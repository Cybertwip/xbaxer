// cleng_driver_inc.cpp — pull Clang's driver.cpp into our cgo bridge.
//
// The upstream clang/tools/driver/driver.cpp is the only TU that defines
// `clang_main`. It is compiled into the `clang` executable, not into any
// installed static library, so we cannot link against it from the prefix
// produced by build-clang-engine.sh. Including the source file directly
// here makes `clang_main` part of our cgo build and bridge.cpp can call
// it. driver.cpp uses GENERATE_DRIVER mode in modern Clang, so it does
// NOT define a plain `int main` — there is no symbol collision with Go's
// runtime entry point.
#include "../../../llvm-project/clang/tools/driver/driver.cpp"
