# CMake toolchain file for cross-compiling to Windows / amd64 using the
# mingw-w64 toolchain (provided by Homebrew's `mingw-w64` on macOS or by
# the `mingw-w64`/`gcc-mingw-w64-x86-64` packages on Debian/Ubuntu/Fedora).
#
# Used by:
#   * the LLVM cross-build that produces the Windows static libs in
#     ${CMAKE_BINARY_DIR}/engine/clang-windows-amd64/, and
#   * (indirectly) cleng itself, which links those static libs via cgo.
#
# Pass to cmake with:  -DCMAKE_TOOLCHAIN_FILE=<this-file>

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${_TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${_TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${_TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${_TOOLCHAIN_PREFIX}-ar)
set(CMAKE_RANLIB       ${_TOOLCHAIN_PREFIX}-ranlib)

# Common search roots for mingw-w64 sysroots across Homebrew (Intel + Apple
# Silicon) and the typical Debian/Ubuntu/Fedora layouts.
set(CMAKE_FIND_ROOT_PATH
  /usr/local/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32
  /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32
  /usr/x86_64-w64-mingw32
  /usr/local/x86_64-w64-mingw32
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static everything — we want self-contained PE archives.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
