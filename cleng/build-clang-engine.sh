#!/usr/bin/env bash
# build-clang-engine.sh — bootstrap LLVM/Clang for the cleng cgo bridge.
#
# Mirrors the pattern used by campiler/build-campiler.sh: take a vendored
# upstream source tree (here ./llvm-project, added as a git submodule pinned
# at llvmorg-22.1.3), build it twice — once natively to produce the
# tablegen tools, then once cross-compiled to windows/amd64 — and install
# the result as a self-contained prefix that cleng will later link against.
#
# Usage:
#   build-clang-engine.sh <llvm-source-dir> <work-dir> <output-dir> \
#                         <toolchain-file> [host-clang] [host-clang++]
#
# On success, <output-dir> contains:
#   include/                # Clang + LLVM headers (windows-targeted)
#   lib/libclang*.a         # static archives consumed by cleng's cgo bridge
#   lib/libLLVM*.a
#
# Re-running this script is incremental: both stages use Ninja and a stamp
# file gates the heavy work so a successful build is not redone.

set -euo pipefail

if [[ "$#" -lt 4 || "$#" -gt 6 ]]; then
  echo "usage: $0 <llvm-source-dir> <work-dir> <output-dir> <toolchain-file> [host-clang] [host-clang++]" >&2
  exit 2
fi

LLVM_SRC="$1"
WORK_DIR="$2"
OUTPUT_DIR="$3"
TOOLCHAIN_FILE="$4"
HOST_CC="${5:-clang}"
HOST_CXX="${6:-clang++}"

NATIVE_BUILD="$WORK_DIR/native"
CROSS_BUILD="$WORK_DIR/cross"
NATIVE_STAMP="$NATIVE_BUILD/.tblgen-built"
CROSS_STAMP="$OUTPUT_DIR/.clang-engine-installed"

# Native binaries get a .exe suffix when this script runs under Git Bash /
# MSYS on a Windows host (the native stage uses MSVC or clang-cl). Detect
# that up front so the cross stage's LLVM_TABLEGEN / CLANG_TABLEGEN cache
# entries point at files that actually exist.
NATIVE_EXE_SUFFIX=""
case "${OS:-}${OSTYPE:-}" in
  *Windows_NT*|*msys*|*cygwin*|*mingw*) NATIVE_EXE_SUFFIX=".exe" ;;
esac

mkdir -p "$NATIVE_BUILD" "$CROSS_BUILD" "$OUTPUT_DIR"

cross_cache_var_equals() {
  local key="$1"
  local expected="$2"
  local cache="$CROSS_BUILD/CMakeCache.txt"
  [[ -f "$cache" ]] || return 1
  grep -Eq "^${key}(:[A-Z]+)?=${expected}\$" "$cache"
}

cross_cache_var_contains() {
  local key="$1"
  local needle="$2"
  local cache="$CROSS_BUILD/CMakeCache.txt"
  [[ -f "$cache" ]] || return 1
  grep -E "^${key}(:[A-Z]+)?=" "$cache" | grep -Fq "$needle"
}

has_cross_linker() {
  local path
  for path in \
    "$OUTPUT_DIR/bin/lld.exe" \
    "$OUTPUT_DIR/bin/ld64.lld.exe" \
    "$OUTPUT_DIR/bin/ld.lld.exe" \
    "$CROSS_BUILD/bin/lld.exe" \
    "$CROSS_BUILD/bin/ld64.lld.exe" \
    "$CROSS_BUILD/bin/ld.lld.exe"
  do
    [[ -f "$path" ]] && return 0
  done
  return 1
}

# Common LLVM cmake flags shared by both stages. We keep the build minimal:
# just the targets we need for xbax (x86_64 + arm64), no
# tests/benchmarks/examples, no LTO, no docs.
LLVM_COMMON_FLAGS=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DLLVM_ENABLE_PROJECTS=clang\;lld
  -DLLVM_TARGETS_TO_BUILD=X86\;AArch64
  -DLLVM_INCLUDE_TESTS=OFF
  -DLLVM_INCLUDE_BENCHMARKS=OFF
  -DLLVM_INCLUDE_EXAMPLES=OFF
  -DLLVM_INCLUDE_DOCS=OFF
  -DCLANG_INCLUDE_TESTS=OFF
  -DCLANG_INCLUDE_DOCS=OFF
  -DLLVM_ENABLE_ZLIB=OFF
  -DLLVM_ENABLE_ZSTD=OFF
  -DLLVM_ENABLE_LIBXML2=OFF
  -DLLVM_ENABLE_TERMINFO=OFF
  -DLLVM_ENABLE_LIBEDIT=OFF
  -DLLVM_ENABLE_LIBPFM=OFF
  -DLLVM_ENABLE_BINDINGS=OFF
  -DLLVM_ENABLE_OCAMLDOC=OFF
  -DBUILD_SHARED_LIBS=OFF
)

# ---- Stage 1: native bootstrap of llvm-tblgen / clang-tblgen ----------------
#
# Cross-compiling LLVM requires native versions of the tablegen drivers to
# generate the .inc files consumed by the rest of the build. We build only
# those two binaries, using the host clang (matching how go's make.bash uses
# GOROOT_BOOTSTRAP).
if [[ ! -f "$NATIVE_STAMP" ]]; then
  echo "[clang-engine] stage 1: native tblgen bootstrap (host CC=$HOST_CC)"
  cmake -S "$LLVM_SRC/llvm" -B "$NATIVE_BUILD" \
    "${LLVM_COMMON_FLAGS[@]}" \
    -DCMAKE_C_COMPILER="$HOST_CC" \
    -DCMAKE_CXX_COMPILER="$HOST_CXX" \
    -DLLVM_BUILD_TOOLS=OFF \
    -DLLVM_BUILD_UTILS=ON \
    -DCLANG_BUILD_TOOLS=OFF
  cmake --build "$NATIVE_BUILD" --target llvm-tblgen clang-tblgen llvm-min-tblgen
  : > "$NATIVE_STAMP"
fi

NATIVE_LLVM_TBLGEN="$NATIVE_BUILD/bin/llvm-tblgen$NATIVE_EXE_SUFFIX"
NATIVE_CLANG_TBLGEN="$NATIVE_BUILD/bin/clang-tblgen$NATIVE_EXE_SUFFIX"

if [[ ! -x "$NATIVE_LLVM_TBLGEN" || ! -x "$NATIVE_CLANG_TBLGEN" ]]; then
  echo "[clang-engine] stage 1 failed: tblgen binaries missing under $NATIVE_BUILD/bin" >&2
  ls -la "$NATIVE_BUILD/bin" >&2 || true
  exit 1
fi

# ---- Stage 2: cross-compile to windows/amd64 -------------------------------
#
# We install the static libs + headers that cleng links against, plus the
# lld linker executables that led.exe dispatches to at runtime.
# cleng provides its own driver entry point in Go and links against
# clang_main from libclangDriver via the C++ shim in
# cleng/internal/cgobridge/bridge.cpp.
NEED_CROSS_BUILD=0
if [[ ! -f "$CROSS_STAMP" ]]; then
  NEED_CROSS_BUILD=1
fi
if ! cross_cache_var_equals LLVM_BUILD_TOOLS ON; then
  NEED_CROSS_BUILD=1
fi
if ! cross_cache_var_equals LLD_BUILD_TOOLS ON; then
  NEED_CROSS_BUILD=1
fi
if ! cross_cache_var_contains LLVM_ENABLE_PROJECTS "lld"; then
  NEED_CROSS_BUILD=1
fi
if ! cross_cache_var_contains LLVM_TARGETS_TO_BUILD "AArch64"; then
  NEED_CROSS_BUILD=1
fi
if ! has_cross_linker; then
  NEED_CROSS_BUILD=1
fi

if [[ "$NEED_CROSS_BUILD" -eq 1 ]]; then
  echo "[clang-engine] stage 2: cross-compile clang static libs for windows/amd64"
  rm -f "$CROSS_STAMP"
  cmake -S "$LLVM_SRC/llvm" -B "$CROSS_BUILD" \
    "${LLVM_COMMON_FLAGS[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_INSTALL_PREFIX="$OUTPUT_DIR" \
    -DLLVM_TABLEGEN="$NATIVE_LLVM_TBLGEN" \
    -DCLANG_TABLEGEN="$NATIVE_CLANG_TBLGEN" \
    -DLLVM_HOST_TRIPLE=x86_64-w64-mingw32 \
    -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-w64-mingw32 \
    -DLLVM_BUILD_TOOLS=ON \
    -DLLVM_BUILD_UTILS=OFF \
    -DLLVM_INCLUDE_TOOLS=ON \
    -DLLD_BUILD_TOOLS=ON \
    -DCLANG_BUILD_TOOLS=OFF \
    -DLLVM_ENABLE_PIC=OFF
  # install-clang-resource-headers ships clang's compiler-builtin headers
  # (stddef.h, stdarg.h, stdint.h, intrinsic wrappers, etc.) into
  # ${OUTPUT_DIR}/lib/clang/<major>/include. These are NOT platform SDK
  # headers — they're the freestanding C/C++ subset that the compiler
  # itself owns and must always be reachable, regardless of GOOS. cleng
  # locates them automatically via its `<bin>/../lib/clang/<ver>/include`
  # search rule, so dropping them in the prefix means a stock cleng.exe
  # can compile any cgo TU that only uses freestanding headers without
  # needing a platform SDK on the console.
  cmake --build "$CROSS_BUILD" --target install-clang-libraries install-llvm-libraries install-clang-headers install-llvm-headers install-clang-resource-headers install-lld
  : > "$CROSS_STAMP"
fi

echo "[clang-engine] done — installed prefix: $OUTPUT_DIR"
