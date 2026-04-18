cmake_minimum_required(VERSION 3.21)
project(xbax-stream LANGUAGES NONE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(POWER_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

# ==============================================================================
# Go 1.24.13 toolchain (built at build time, not configure time)
# ==============================================================================
set(CMAKE_FOLDER "Toolchain")
add_subdirectory(go ${POWER_STAGE_DIR}/External/go)
setup_go_target()
set(CMAKE_FOLDER "")
# setup_go_target() sets GO_EXECUTABLE, GO_ROOT_DIR and creates the
# "go-toolchain" target. Anything that needs the Go binary must
# add_dependencies(... go-toolchain).

set(XBAX_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/Xbax")
set(XBAX_HOST_DIR "${CMAKE_BINARY_DIR}/host")
set(CAMPILER_TEMP_DIR "${CMAKE_BINARY_DIR}/engine/go-windows-amd64-bootstrap")
set(CAMPILER_OUTPUT_DIR "${XBAX_PACKAGE_DIR}/campiler")
set(CAMPILER_OUTPUT "${CAMPILER_OUTPUT_DIR}/bin/go.exe")
set(CAMPILER_SCRIPT "${CMAKE_SOURCE_DIR}/campiler/build-campiler.sh")

set(XBAX_GO_BUILD_ENV
  "GOROOT=${GO_ROOT_DIR}"
  "GOTOOLCHAIN=local"
  "GOCACHE=${CMAKE_BINARY_DIR}/go-cache"
  "GOMODCACHE=${CMAKE_BINARY_DIR}/go-mod-cache"
  "GOPATH=${CMAKE_BINARY_DIR}/go-path"
  "GOOS=windows"
  "GOARCH=amd64"
  "CGO_ENABLED=0"
)

set(XBAX_HOST_GO_BUILD_ENV
  "GOROOT=${GO_ROOT_DIR}"
  "GOTOOLCHAIN=local"
  "GOCACHE=${CMAKE_BINARY_DIR}/host-go-cache"
  "GOMODCACHE=${CMAKE_BINARY_DIR}/host-go-mod-cache"
  "GOPATH=${CMAKE_BINARY_DIR}/host-go-path"
)

set(HOST_GO_EXECUTABLE_SUFFIX "")
if(CMAKE_HOST_WIN32)
  set(HOST_GO_EXECUTABLE_SUFFIX ".exe")
endif()

add_custom_command(
  OUTPUT "${CAMPILER_OUTPUT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${XBAX_PACKAGE_DIR}"
  COMMAND bash "${CAMPILER_SCRIPT}"
          "${CMAKE_SOURCE_DIR}/go"
          "${GO_ROOT_DIR}"
          "${CAMPILER_TEMP_DIR}"
          "${CAMPILER_OUTPUT_DIR}"
  DEPENDS
    go-toolchain
    "${CMAKE_SOURCE_DIR}/go/src/make.bash"
    "${CAMPILER_SCRIPT}"
  COMMENT "Bootstrapping campiler for windows/amd64 using the custom Go toolchain"
  VERBATIM
)

add_custom_target(campiler DEPENDS "${CAMPILER_OUTPUT}")
set_target_properties(campiler PROPERTIES FOLDER "Apps")

function(add_xbax_windows_go_tool tool_name)
  if(ARGC GREATER 1)
    set(_source_dir "${CMAKE_SOURCE_DIR}/${ARGV1}")
  else()
    set(_source_dir "${CMAKE_SOURCE_DIR}/${tool_name}")
  endif()
  set(_output_dir "${XBAX_PACKAGE_DIR}/${tool_name}")
  set(_output "${_output_dir}/${tool_name}.exe")
  set(_deps
    go-toolchain
    "${_source_dir}/go.mod"
    "${_source_dir}/main.go"
  )
  if(EXISTS "${_source_dir}/go.sum")
    list(APPEND _deps "${_source_dir}/go.sum")
  endif()

  add_custom_command(
    OUTPUT "${_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-cache"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-mod-cache"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-path"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
    COMMAND ${CMAKE_COMMAND} -E env ${XBAX_GO_BUILD_ENV}
            "${GO_EXECUTABLE}" build -buildvcs=false -trimpath
            -o "${_output}" .
    DEPENDS ${_deps}
    WORKING_DIRECTORY "${_source_dir}"
    COMMENT "Building ${tool_name}.exe with the custom Go toolchain"
    VERBATIM
  )

  add_custom_target("${tool_name}" DEPENDS "${_output}")
  set_target_properties("${tool_name}" PROPERTIES FOLDER "Apps")
endfunction()

function(add_xbax_host_go_tool tool_name)
  if(ARGC GREATER 1)
    set(_source_dir "${CMAKE_SOURCE_DIR}/${ARGV1}")
  else()
    set(_source_dir "${CMAKE_SOURCE_DIR}/${tool_name}")
  endif()
  set(_output_dir "${XBAX_HOST_DIR}/${tool_name}")
  set(_output "${_output_dir}/${tool_name}${HOST_GO_EXECUTABLE_SUFFIX}")
  set(_deps
    go-toolchain
    "${_source_dir}/go.mod"
    "${_source_dir}/main.go"
  )
  if(EXISTS "${_source_dir}/go.sum")
    list(APPEND _deps "${_source_dir}/go.sum")
  endif()

  add_custom_command(
    OUTPUT "${_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/host-go-cache"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/host-go-mod-cache"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/host-go-path"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
    COMMAND ${CMAKE_COMMAND} -E env ${XBAX_HOST_GO_BUILD_ENV}
            "${GO_EXECUTABLE}" build -buildvcs=false -trimpath
            -o "${_output}" .
    DEPENDS ${_deps}
    WORKING_DIRECTORY "${_source_dir}"
    COMMENT "Building host ${tool_name}${HOST_GO_EXECUTABLE_SUFFIX} with the custom Go toolchain"
    VERBATIM
  )

  add_custom_target("host-${tool_name}" DEPENDS "${_output}")
  set_target_properties("host-${tool_name}" PROPERTIES FOLDER "Apps")
endfunction()

add_xbax_windows_go_tool(dastrabution)
add_xbax_windows_go_tool(anzipper)
add_xbax_windows_go_tool(gatter)
add_xbax_windows_go_tool(sarver Sarver)
add_xbax_windows_go_tool(cliant Cliant)
add_xbax_host_go_tool(cliant Cliant)

# ==============================================================================
# cleng — Go-fronted Clang driver (cgo, cross-compiled to windows/amd64)
#
# Unlike the other Go tools above, cleng links against the prebuilt LLVM/Clang
# static libraries via cgo. It therefore needs CGO_ENABLED=1 plus a working
# x86_64-w64-mingw32 C/C++ cross toolchain on the host (e.g. Homebrew's
# `mingw-w64`). The Clang libraries themselves are expected under
# ${CLENG_CLANG_PREFIX} (defaults to ${CMAKE_BINARY_DIR}/engine/clang-windows-amd64);
# how that prefix is populated is intentionally out of scope here — the LLVM
# project is vendored as a git submodule under ./llvm-project for a future
# build target to consume.
# ==============================================================================
set(CLENG_SOURCE_DIR "${CMAKE_SOURCE_DIR}/cleng")
set(CLENG_OUTPUT_DIR "${XBAX_PACKAGE_DIR}/cleng/bin")
set(CLENG_OUTPUT "${CLENG_OUTPUT_DIR}/cleng.exe")
set(LED_SOURCE_DIR "${CMAKE_SOURCE_DIR}/led")
set(LED_OUTPUT "${CLENG_OUTPUT_DIR}/led.exe")
set(CLENG_CLANG_PREFIX "${CMAKE_BINARY_DIR}/engine/clang-windows-amd64"
    CACHE PATH "Prefix containing prebuilt Clang/LLVM static libs and headers for windows/amd64")

function(cleng_real_parent_dir out_var in_path)
  if(NOT in_path OR in_path STREQUAL "" OR NOT EXISTS "${in_path}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  get_filename_component(_real "${in_path}" REALPATH)
  get_filename_component(_dir "${_real}" DIRECTORY)
  get_filename_component(_parent "${_dir}" DIRECTORY)
  set(${out_var} "${_parent}" PARENT_SCOPE)
endfunction()

function(cleng_set_first_existing_dir out_var)
  foreach(_dir IN LISTS ARGN)
    if(EXISTS "${_dir}")
      set(${out_var} "${_dir}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(cleng_find_existing_child_dir out_var base)
  if(NOT base OR base STREQUAL "" OR NOT EXISTS "${base}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  foreach(_rel IN LISTS ARGN)
    if(EXISTS "${base}/${_rel}")
      set(${out_var} "${base}/${_rel}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

set(CLENG_RUNTIME_BUNDLE_COMMANDS)
function(cleng_append_runtime_bundle_command source dest marker components)
  foreach(_component IN LISTS components)
    list(APPEND CLENG_RUNTIME_BUNDLE_COMMANDS
      COMMAND ${CMAKE_COMMAND}
              -DSYSROOT_SOURCE=${source}
              -DSYSROOT_DEST=${dest}
              -DSYSROOT_MARKER=${marker}
              -DSYSROOT_COMPONENTS=${_component}
              -DSYSROOT_PRESERVE_DEST=ON
              -P "${CLENG_SOURCE_DIR}/scripts/copy_sysroot.cmake"
    )
  endforeach()
  set(CLENG_RUNTIME_BUNDLE_COMMANDS "${CLENG_RUNTIME_BUNDLE_COMMANDS}" PARENT_SCOPE)
endfunction()

find_program(MINGW_CC  x86_64-w64-mingw32-gcc)
find_program(MINGW_CXX x86_64-w64-mingw32-g++)
if(NOT MINGW_CC OR NOT MINGW_CXX)
  set(MINGW_CC  "x86_64-w64-mingw32-gcc"  CACHE FILEPATH "Windows/amd64 C cross compiler")
  set(MINGW_CXX "x86_64-w64-mingw32-g++"  CACHE FILEPATH "Windows/amd64 C++ cross compiler")
endif()

# ---- Locate the per-triplet headers / runtime libs we will bundle ----------
#
# The clang resource directory ships compiler-builtin headers only (stddef.h,
# intrinsics, …). Standard C headers (stdio.h, string.h) and the C++
# standard library (vector, sstream, …) are NOT part of the compiler — they
# come from the target libc / libstdc++ / libc++ / SDK. We therefore bundle
# the target-facing, redistributable pieces we can harvest on the build host:
#
#   * windows/amd64: mingw-w64 headers + import libs + GCC runtime archives
#   * darwin/{amd64,arm64}: the active Apple SDK's headers/framework stubs
#     when we are configuring on an Apple host with xcrun available
#   * linux/{amd64,arm64}: glibc/libstdc++ headers + runtime archives from the
#     GNU cross toolchains, when those toolchains are installed on the host
#
# Layout we end up shipping at runtime (sibling of cleng/bin/cleng.exe):
#   cleng/sysroot/x86_64-w64-mingw32/include/                      (C/C++)
#   cleng/sysroot/x86_64-w64-mingw32/lib/                          (PE import libs)
#   cleng/sysroot/x86_64-w64-mingw32/lib/gcc/...                   (libgcc, crt*)
#   cleng/sysroot/x86_64-apple-darwin/usr/include + /usr/lib       (macOS SDK)
#   cleng/sysroot/x86_64-apple-darwin/System/Library/*Frameworks   (framework stubs/headers)
#   cleng/sysroot/x86_64-apple-darwin/Library/Frameworks
#   cleng/sysroot/aarch64-apple-darwin/...
#   cleng/sysroot/x86_64-linux-gnu/sysroot/{usr/include,usr/lib,...}(glibc sysroot)
#   cleng/sysroot/x86_64-linux-gnu/include/c++/...                 (libstdc++)
#   cleng/sysroot/x86_64-linux-gnu/lib/gcc/...                     (libgcc, crt*)
#   cleng/sysroot/aarch64-linux-gnu/...
#
# cleng injects the matching sysroot / search-path flags at runtime based on
# the target triple. When a bundle is absent for a given target, we leave the
# driver untouched and the developer can still provide their own sysroot via
# explicit `--sysroot=` / `-isysroot` / `-L` / `-isystem` flags.
if(DEFINED CLENG_MINGW_SYSROOT AND (NOT DEFINED CLENG_MINGW_TOOLCHAIN_ROOT OR CLENG_MINGW_TOOLCHAIN_ROOT STREQUAL ""))
  get_filename_component(CLENG_MINGW_TOOLCHAIN_ROOT "${CLENG_MINGW_SYSROOT}" DIRECTORY)
endif()
if(NOT DEFINED CLENG_MINGW_SYSROOT)
  # Walk back from x86_64-w64-mingw32-gcc to its sibling
  # x86_64-w64-mingw32/ directory (which contains include/ and lib/).
  cleng_real_parent_dir(_mingw_root "${MINGW_CC}")
  if(_mingw_root)
    if(EXISTS "${_mingw_root}/x86_64-w64-mingw32/include/stdio.h")
      set(CLENG_MINGW_SYSROOT "${_mingw_root}/x86_64-w64-mingw32"
          CACHE PATH "mingw-w64 sysroot whose include/ tree is bundled with cleng")
      set(CLENG_MINGW_TOOLCHAIN_ROOT "${_mingw_root}"
          CACHE PATH "mingw-w64 toolchain root whose runtime archives are bundled with cleng")
    endif()
  endif()
endif()
if(NOT DEFINED CLENG_MINGW_SYSROOT)
  # Homebrew default if auto-detection failed.
  file(GLOB _mingw_brew "/usr/local/Cellar/mingw-w64/*/toolchain-x86_64"
                         "/opt/homebrew/Cellar/mingw-w64/*/toolchain-x86_64")
  if(_mingw_brew)
    list(GET _mingw_brew 0 _mingw_first)
    if(EXISTS "${_mingw_first}/x86_64-w64-mingw32/include/stdio.h")
      set(CLENG_MINGW_SYSROOT "${_mingw_first}/x86_64-w64-mingw32"
          CACHE PATH "mingw-w64 sysroot whose include/ tree is bundled with cleng")
      set(CLENG_MINGW_TOOLCHAIN_ROOT "${_mingw_first}"
          CACHE PATH "mingw-w64 toolchain root whose runtime archives are bundled with cleng")
    endif()
  endif()
endif()
if(DEFINED CLENG_MINGW_SYSROOT)
  message(STATUS "cleng sysroot (mingw-w64): ${CLENG_MINGW_SYSROOT}")
else()
  message(WARNING "cleng: no mingw-w64 sysroot detected; cleng.exe will ship without bundled Windows headers/runtime libs. Set -DCLENG_MINGW_SYSROOT=/path/to/x86_64-w64-mingw32 to override.")
endif()

if(APPLE AND NOT DEFINED CLENG_APPLE_SDKROOT)
  find_program(CLENG_XCRUN_EXECUTABLE xcrun)
  if(CLENG_XCRUN_EXECUTABLE)
    execute_process(
      COMMAND "${CLENG_XCRUN_EXECUTABLE}" --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE _cleng_apple_sdk
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(EXISTS "${_cleng_apple_sdk}/usr/include/stdio.h")
      set(CLENG_APPLE_SDKROOT "${_cleng_apple_sdk}"
          CACHE PATH "Apple SDK root whose headers/framework stubs are bundled with cleng")
    endif()
  endif()
endif()
if(APPLE)
  if(DEFINED CLENG_APPLE_SDKROOT AND EXISTS "${CLENG_APPLE_SDKROOT}/usr/include/stdio.h")
    message(STATUS "cleng sysroot (apple sdk): ${CLENG_APPLE_SDKROOT}")
  else()
    message(WARNING "cleng: no Apple SDK detected; darwin targets will ship without a bundled SDK. Set -DCLENG_APPLE_SDKROOT=/path/to/MacOSX.sdk to override.")
  endif()
endif()

find_program(CLENG_LINUX_AMD64_GCC NAMES x86_64-linux-gnu-gcc x86_64-unknown-linux-gnu-gcc)
if(NOT DEFINED CLENG_LINUX_AMD64_TOOLCHAIN_ROOT)
  cleng_real_parent_dir(_linux_amd64_toolchain "${CLENG_LINUX_AMD64_GCC}")
  if(NOT _linux_amd64_toolchain)
    cleng_set_first_existing_dir(_linux_amd64_toolchain
      "/usr/local/opt/x86_64-unknown-linux-gnu/toolchain"
      "/opt/homebrew/opt/x86_64-unknown-linux-gnu/toolchain")
  endif()
  if(_linux_amd64_toolchain)
    set(CLENG_LINUX_AMD64_TOOLCHAIN_ROOT "${_linux_amd64_toolchain}"
        CACHE PATH "Linux/amd64 GNU cross toolchain root whose headers/runtime libs are bundled with cleng")
  endif()
endif()
if(DEFINED CLENG_LINUX_AMD64_TOOLCHAIN_ROOT AND NOT DEFINED CLENG_LINUX_AMD64_TARGET_ROOT)
  cleng_find_existing_child_dir(_linux_amd64_target "${CLENG_LINUX_AMD64_TOOLCHAIN_ROOT}"
    "x86_64-linux-gnu"
    "x86_64-unknown-linux-gnu")
  if(_linux_amd64_target AND EXISTS "${_linux_amd64_target}/sysroot/usr/include/stdio.h")
    set(CLENG_LINUX_AMD64_TARGET_ROOT "${_linux_amd64_target}"
        CACHE PATH "Linux/amd64 target-data root whose glibc/libstdc++ headers are bundled with cleng")
  endif()
endif()
if(DEFINED CLENG_LINUX_AMD64_TARGET_ROOT)
  message(STATUS "cleng sysroot (linux/amd64): ${CLENG_LINUX_AMD64_TARGET_ROOT}")
else()
  message(STATUS "cleng sysroot (linux/amd64): not detected; install x86_64-unknown-linux-gnu to bundle Linux/amd64 headers/runtime libs")
endif()

find_program(CLENG_LINUX_ARM64_GCC NAMES aarch64-linux-gnu-gcc aarch64-unknown-linux-gnu-gcc)
if(NOT DEFINED CLENG_LINUX_ARM64_TOOLCHAIN_ROOT)
  cleng_real_parent_dir(_linux_arm64_toolchain "${CLENG_LINUX_ARM64_GCC}")
  if(NOT _linux_arm64_toolchain)
    cleng_set_first_existing_dir(_linux_arm64_toolchain
      "/usr/local/opt/aarch64-unknown-linux-gnu/toolchain"
      "/opt/homebrew/opt/aarch64-unknown-linux-gnu/toolchain")
  endif()
  if(_linux_arm64_toolchain)
    set(CLENG_LINUX_ARM64_TOOLCHAIN_ROOT "${_linux_arm64_toolchain}"
        CACHE PATH "Linux/arm64 GNU cross toolchain root whose headers/runtime libs are bundled with cleng")
  endif()
endif()
if(DEFINED CLENG_LINUX_ARM64_TOOLCHAIN_ROOT AND NOT DEFINED CLENG_LINUX_ARM64_TARGET_ROOT)
  cleng_find_existing_child_dir(_linux_arm64_target "${CLENG_LINUX_ARM64_TOOLCHAIN_ROOT}"
    "aarch64-linux-gnu"
    "aarch64-unknown-linux-gnu")
  if(_linux_arm64_target AND EXISTS "${_linux_arm64_target}/sysroot/usr/include/stdio.h")
    set(CLENG_LINUX_ARM64_TARGET_ROOT "${_linux_arm64_target}"
        CACHE PATH "Linux/arm64 target-data root whose glibc/libstdc++ headers are bundled with cleng")
  endif()
endif()
if(DEFINED CLENG_LINUX_ARM64_TARGET_ROOT)
  message(STATUS "cleng sysroot (linux/arm64): ${CLENG_LINUX_ARM64_TARGET_ROOT}")
else()
  message(STATUS "cleng sysroot (linux/arm64): not detected; install aarch64-unknown-linux-gnu to bundle Linux/arm64 headers/runtime libs")
endif()

cleng_append_runtime_bundle_command(
  "${CLENG_MINGW_SYSROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-w64-mingw32"
  "include/stdio.h"
  "include;lib"
)
cleng_append_runtime_bundle_command(
  "${CLENG_MINGW_TOOLCHAIN_ROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-w64-mingw32"
  "lib/gcc/x86_64-w64-mingw32"
  "lib/gcc"
)
cleng_append_runtime_bundle_command(
  "${CLENG_APPLE_SDKROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-apple-darwin"
  "usr/include/stdio.h"
  "usr/include;usr/lib;System/Library/Frameworks;System/Library/PrivateFrameworks;Library/Frameworks"
)
cleng_append_runtime_bundle_command(
  "${CLENG_APPLE_SDKROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/aarch64-apple-darwin"
  "usr/include/stdio.h"
  "usr/include;usr/lib;System/Library/Frameworks;System/Library/PrivateFrameworks;Library/Frameworks"
)
cleng_append_runtime_bundle_command(
  "${CLENG_LINUX_AMD64_TARGET_ROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-linux-gnu"
  "sysroot/usr/include/stdio.h"
  "include;lib;lib32;lib64;sysroot/include;sysroot/lib;sysroot/lib64;sysroot/usr/include;sysroot/usr/lib;sysroot/usr/lib64"
)
cleng_append_runtime_bundle_command(
  "${CLENG_LINUX_AMD64_TOOLCHAIN_ROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-linux-gnu"
  "lib/gcc"
  "lib/gcc"
)
cleng_append_runtime_bundle_command(
  "${CLENG_LINUX_ARM64_TARGET_ROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/aarch64-linux-gnu"
  "sysroot/usr/include/stdio.h"
  "include;lib;lib32;lib64;sysroot/include;sysroot/lib;sysroot/lib64;sysroot/usr/include;sysroot/usr/lib;sysroot/usr/lib64"
)
cleng_append_runtime_bundle_command(
  "${CLENG_LINUX_ARM64_TOOLCHAIN_ROOT}"
  "${XBAX_PACKAGE_DIR}/cleng/sysroot/aarch64-linux-gnu"
  "lib/gcc"
  "lib/gcc"
)

# ---- clang-engine: cross-build LLVM/Clang static libs for windows/amd64 ----
#
# Mirrors the way `campiler` consumes the vendored ./go source tree: take the
# `llvm-project` git submodule, run a script that does a two-stage build
# (native tblgen bootstrap, then mingw-w64 cross compile), and install the
# Windows-targeted static libs + headers under ${CLENG_CLANG_PREFIX}. cleng
# itself depends on this target so `cmake --build build --target cleng`
# transparently triggers the engine build first.
set(CLANG_ENGINE_SCRIPT "${CLENG_SOURCE_DIR}/build-clang-engine.sh")
set(CLANG_ENGINE_TOOLCHAIN_FILE "${CLENG_SOURCE_DIR}/cmake/mingw-w64-x86_64.cmake")
set(CLANG_ENGINE_WORK_DIR "${CMAKE_BINARY_DIR}/engine/clang-build")
set(CLANG_ENGINE_STAMP "${CLENG_CLANG_PREFIX}/.clang-engine-installed")

# Auto-detect a host clang the same way go/CMakeLists.txt finds GOROOT_BOOTSTRAP.
# Honour an explicit -DCLENG_HOST_CLANG=... if provided.
if(NOT DEFINED CLENG_HOST_CLANG)
  find_program(CLENG_HOST_CLANG NAMES clang)
endif()
if(NOT DEFINED CLENG_HOST_CLANGXX)
  find_program(CLENG_HOST_CLANGXX NAMES clang++)
endif()
if(NOT CLENG_HOST_CLANG OR NOT CLENG_HOST_CLANGXX)
  message(STATUS "cleng: host clang not auto-detected; clang-engine target will rely on PATH (clang/clang++)")
  if(NOT CLENG_HOST_CLANG)
    set(CLENG_HOST_CLANG "clang")
  endif()
  if(NOT CLENG_HOST_CLANGXX)
    set(CLENG_HOST_CLANGXX "clang++")
  endif()
endif()

add_custom_command(
  OUTPUT "${CLANG_ENGINE_STAMP}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CLENG_CLANG_PREFIX}"
  COMMAND bash "${CLANG_ENGINE_SCRIPT}"
          "${CMAKE_SOURCE_DIR}/llvm-project"
          "${CLANG_ENGINE_WORK_DIR}"
          "${CLENG_CLANG_PREFIX}"
          "${CLANG_ENGINE_TOOLCHAIN_FILE}"
          "${CLENG_HOST_CLANG}"
          "${CLENG_HOST_CLANGXX}"
  DEPENDS
    "${CLANG_ENGINE_SCRIPT}"
    "${CLANG_ENGINE_TOOLCHAIN_FILE}"
  COMMENT "Building Clang/LLVM static libs for windows/amd64 (cleng engine)"
  VERBATIM
)

add_custom_target(clang-engine DEPENDS "${CLANG_ENGINE_STAMP}")
set_target_properties(clang-engine PROPERTIES FOLDER "Toolchain")
set(CLENG_LINKER_BIN_SOURCE "${CLANG_ENGINE_WORK_DIR}/cross/bin")

# Generate bridge_link_generated.go listing every libclang*.a / libLLVM*.a in
# the cleng prefix. The Go tooling parses #cgo LDFLAGS at compile time, so the
# generated file must be on disk before the cleng go-build step runs. We
# regenerate on every build (cheap — a single CMake glob) so newly installed
# archives are picked up without a manual reconfigure.
set(CLENG_LINK_GENERATED "${CLENG_SOURCE_DIR}/internal/cgobridge/bridge_link_generated.go")
add_custom_command(
  OUTPUT "${CLENG_LINK_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DCLENG_PREFIX=${CLENG_CLANG_PREFIX}
          -DOUT_FILE=${CLENG_LINK_GENERATED}
          -P "${CLENG_SOURCE_DIR}/scripts/gen_bridge_link.cmake"
  DEPENDS
    "${CLANG_ENGINE_STAMP}"
    "${CLENG_SOURCE_DIR}/scripts/gen_bridge_link.cmake"
  COMMENT "Generating cleng cgo link list from ${CLENG_CLANG_PREFIX}/lib"
  VERBATIM
)

set(XBAX_CLENG_BUILD_ENV
  ${XBAX_GO_BUILD_ENV}
  "CGO_ENABLED=1"
  "CC=${MINGW_CC}"
  "CXX=${MINGW_CXX}"
)
# CXXFLAGS / LDFLAGS for the LLVM prefix come from bridge_link_generated.go;
# anything extra goes through the #cgo directives in bridge_link.go.
# CGO_ENABLED=1 must override the CGO_ENABLED=0 that XBAX_GO_BUILD_ENV
# sets, so it appears later in the list above (CMake env wins on last value).

add_custom_command(
  OUTPUT "${LED_OUTPUT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-mod-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-path"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CLENG_OUTPUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E env ${XBAX_GO_BUILD_ENV}
          "${GO_EXECUTABLE}" build -buildvcs=false -trimpath
          -o "${LED_OUTPUT}" .
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LED_OUTPUT}" "${CLENG_OUTPUT_DIR}/ld.lld.exe"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LED_OUTPUT}" "${CLENG_OUTPUT_DIR}/ld64.lld.exe"
  DEPENDS
    go-toolchain
    "${LED_SOURCE_DIR}/go.mod"
    "${LED_SOURCE_DIR}/main.go"
  WORKING_DIRECTORY "${LED_SOURCE_DIR}"
  COMMENT "Building led.exe (bundled linker wrapper) for windows/amd64"
  VERBATIM
)

add_custom_target(led DEPENDS "${LED_OUTPUT}")
set_target_properties(led PROPERTIES FOLDER "Apps")

add_custom_command(
  OUTPUT "${CLENG_OUTPUT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-mod-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-path"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CLENG_OUTPUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E env ${XBAX_CLENG_BUILD_ENV}
          "${GO_EXECUTABLE}" build -buildvcs=false -trimpath
          -o "${CLENG_OUTPUT}" .
  # Bundle clang's compiler-builtin headers (stddef.h, stdarg.h, stdint.h,
  # intrinsics) next to cleng.exe so the deployed driver can resolve them
  # via its <bin>/../lib/clang/<ver>/include search path. These are the
  # *compiler's own* freestanding headers — not a platform SDK; the
  # developer is still responsible for shipping libc/SDK headers for
  # whichever GOOS they target.
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${XBAX_PACKAGE_DIR}/cleng/lib/clang"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CLENG_CLANG_PREFIX}/lib/clang"
          "${XBAX_PACKAGE_DIR}/cleng/lib/clang"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${XBAX_PACKAGE_DIR}/cleng/sysroot"
  # Bundle the linker backends that led.exe dispatches to. These are built
  # from LLVM's lld project and live next to cleng.exe in the deployed tree
  # so the wrapper can resolve them relative to itself.
  COMMAND ${CMAKE_COMMAND}
          -DSYSROOT_SOURCE=${CLENG_LINKER_BIN_SOURCE}
          -DSYSROOT_DEST=${CLENG_OUTPUT_DIR}
          -DSYSROOT_MARKERS=lld.exe\;ld64.lld.exe\;ld.lld.exe
          -DSYSROOT_PRESERVE_DEST=ON
          -P "${CLENG_SOURCE_DIR}/scripts/copy_sysroot.cmake"
  # Bundle the target-facing headers/framework stubs/runtime archives that
  # Clang itself does not own: mingw-w64 for windows, the active Apple SDK
  # for darwin when available on Apple hosts, and GNU glibc/libstdc++
  # payloads for linux triplets when the matching cross toolchains are
  # installed. The runtime Go wrapper picks the right bundle by target
  # triple and injects the corresponding sysroot/search flags.
  ${CLENG_RUNTIME_BUNDLE_COMMANDS}
  DEPENDS
    go-toolchain
    clang-engine
    "${CLANG_ENGINE_STAMP}"
    "${CLENG_LINK_GENERATED}"
    "${CLENG_SOURCE_DIR}/go.mod"
    "${CLENG_SOURCE_DIR}/main.go"
    "${CLENG_SOURCE_DIR}/internal/cleng/run.go"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/bridge.go"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/bridge.cpp"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/bridge_link.go"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/cleng_driver_inc.cpp"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/cleng_cc1_main_inc.cpp"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/cleng_cc1as_main_inc.cpp"
    "${CLENG_SOURCE_DIR}/internal/cgobridge/cleng_cc1gen_reproducer_main_inc.cpp"
  WORKING_DIRECTORY "${CLENG_SOURCE_DIR}"
  COMMENT "Building cleng.exe (Go-fronted Clang driver) for windows/amd64"
  VERBATIM
)

add_custom_target(cleng DEPENDS "${CLENG_OUTPUT}" "${LED_OUTPUT}")
set_target_properties(cleng PROPERTIES FOLDER "Apps")

add_custom_target(package-xbax ALL DEPENDS campiler dastrabution anzipper gatter sarver cliant cleng led)
set_target_properties(package-xbax PROPERTIES FOLDER "Apps")

add_custom_target(host-tools ALL DEPENDS host-cliant)
set_target_properties(host-tools PROPERTIES FOLDER "Apps")
