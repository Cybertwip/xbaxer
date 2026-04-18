# Brewfile — macOS development dependencies for xbax-stream.
#
# Install with:
#
#   brew bundle
#
# This pulls in everything needed to:
#   * configure and build the CMake project,
#   * bootstrap the vendored Go 1.24.13 toolchain (needs an existing `go`),
#   * cross-compile the LLVM/Clang engine and `cleng.exe` to windows/amd64
#     using a host clang plus the mingw-w64 cross toolchain,
#   * bundle Linux/glibc target headers + runtime libs for x86_64-linux-gnu
#     and aarch64-linux-gnu via macOS cross toolchains,
#   * capture the active Apple SDK for darwin Mach-O targets on Apple hosts,
#   * run the Python streaming client (`main.py`).

tap "messense/macos-cross-toolchains"

# --- Build system ------------------------------------------------------------
brew "cmake"
brew "ninja"
brew "pkg-config"
brew "git"

# --- Host toolchains used as bootstraps -------------------------------------
# `llvm` ships a recent host clang/clang++/lld that the LLVM cross-build uses
# to produce the native tablegen tools (llvm-tblgen, clang-tblgen) before the
# Windows cross stage runs — exactly the same pattern as `go/CMakeLists.txt`
# auto-detecting an existing `go` for GOROOT_BOOTSTRAP.
brew "llvm"
brew "go"

# --- Windows / amd64 cross toolchain ----------------------------------------
# Used by the cleng cross build (CC=x86_64-w64-mingw32-gcc, ...) and by the
# LLVM cross-compile that produces the Windows static libs cleng links against.
brew "mingw-w64"

# --- Linux / GNU cross toolchains -------------------------------------------
# Used only as source material for the packaged per-triplet bundles under
# cleng/sysroot/: glibc headers, crt objects, libstdc++, libgcc runtimes, etc.
# come from these toolchains when they are installed on the macOS build host.
brew "x86_64-unknown-linux-gnu"
brew "aarch64-unknown-linux-gnu"

# --- Python client ----------------------------------------------------------
brew "python@3.12"
