// bridge.cpp — C-linkage shim that lets cgo invoke Clang's driver.
//
// The `clang_main` symbol is provided by driver.cpp, included into this
// package via cleng_driver_inc.cpp. We do NOT redeclare llvm::ToolContext
// here — the real definition lives in <llvm/Support/LLVMDriver.h> and is
// pulled in transitively by driver.cpp. Defining a stub would ODR-violate.
//
// Linker note: bridge_link_generated.go (produced at CMake build time by
// cleng/scripts/gen_bridge_link.cmake) supplies the full -L/-l flag set
// for every libclang*.a and libLLVM*.a in the prefix, wrapped in
// --start-group/--end-group so the inter-archive cycles resolve.

#include "llvm/Support/LLVMDriver.h"

// clang_main is defined in clang/tools/driver/driver.cpp (pulled in via
// cleng_driver_inc.cpp) with C++ linkage — NOT extern "C". Re-declare it
// the same way or the call will resolve to a mangled name that doesn't
// exist.
int clang_main(int Argc, char **Argv,
               const llvm::ToolContext &ToolContext);

extern "C" int cleng_clang_main(int argc, char **argv) {
  llvm::ToolContext ctx{};
  if (argc > 0 && argv != nullptr) {
    ctx.Path = argv[0];
    ctx.PrependArg = nullptr;
    ctx.NeedsPrependArg = false;
  }
  return clang_main(argc, argv, ctx);
}
