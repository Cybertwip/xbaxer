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
set(CLENG_CLANG_PREFIX "${CMAKE_BINARY_DIR}/engine/clang-windows-amd64"
    CACHE PATH "Prefix containing prebuilt Clang/LLVM static libs and headers for windows/amd64")

find_program(MINGW_CC  x86_64-w64-mingw32-gcc)
find_program(MINGW_CXX x86_64-w64-mingw32-g++)
if(NOT MINGW_CC OR NOT MINGW_CXX)
  set(MINGW_CC  "x86_64-w64-mingw32-gcc"  CACHE FILEPATH "Windows/amd64 C cross compiler")
  set(MINGW_CXX "x86_64-w64-mingw32-g++"  CACHE FILEPATH "Windows/amd64 C++ cross compiler")
endif()

# ---- Locate the mingw-w64 sysroot whose headers we will bundle with cleng --
#
# The clang resource directory ships compiler-builtin headers only (stddef.h,
# intrinsics, …). Standard C headers (stdio.h, string.h) and the C++
# standard library (vector, sstream, …) are NOT part of the compiler — they
# come from libc / libstdc++. For the windows/amd64 target we already
# require Homebrew's mingw-w64 to link cleng itself, so we reuse that same
# toolchain's include tree as the bundled sysroot. The headers are 0BSD/
# public-domain (mingw-w64-headers) and the libstdc++ headers are GPL-with-
# runtime-exception, both of which permit redistribution.
#
# Layout we end up shipping at runtime (sibling of cleng/bin/cleng.exe):
#   cleng/sysroot/x86_64-w64-mingw32/include/                      (C headers)
#   cleng/sysroot/x86_64-w64-mingw32/include/c++/<ver>/            (libstdc++)
#   cleng/sysroot/x86_64-w64-mingw32/include/c++/<ver>/x86_64-w64-mingw32/
#
# cleng injects these as `-isystem` search paths at runtime when the build
# target triple is x86_64-w64-mingw32. Other GOOS targets (darwin/linux/…)
# remain the developer's responsibility — they must supply their own
# sysroot via `--sysroot=` / `-isystem` cgo flags.
if(NOT DEFINED CLENG_MINGW_SYSROOT)
  # Walk back from x86_64-w64-mingw32-gcc to its sibling
  # x86_64-w64-mingw32/ directory (which contains include/ and lib/).
  if(MINGW_CC AND EXISTS "${MINGW_CC}")
    get_filename_component(_mingw_bin "${MINGW_CC}" DIRECTORY)
    get_filename_component(_mingw_root "${_mingw_bin}" DIRECTORY)
    if(EXISTS "${_mingw_root}/x86_64-w64-mingw32/include/stdio.h")
      set(CLENG_MINGW_SYSROOT "${_mingw_root}/x86_64-w64-mingw32"
          CACHE PATH "mingw-w64 sysroot whose include/ tree is bundled with cleng")
    endif()
  endif()
endif()
if(NOT DEFINED CLENG_MINGW_SYSROOT)
  # Homebrew default if auto-detection failed.
  file(GLOB _mingw_brew "/usr/local/Cellar/mingw-w64/*/toolchain-x86_64/x86_64-w64-mingw32"
                         "/opt/homebrew/Cellar/mingw-w64/*/toolchain-x86_64/x86_64-w64-mingw32")
  if(_mingw_brew)
    list(GET _mingw_brew 0 _mingw_first)
    if(EXISTS "${_mingw_first}/include/stdio.h")
      set(CLENG_MINGW_SYSROOT "${_mingw_first}"
          CACHE PATH "mingw-w64 sysroot whose include/ tree is bundled with cleng")
    endif()
  endif()
endif()
if(DEFINED CLENG_MINGW_SYSROOT)
  message(STATUS "cleng sysroot (mingw-w64): ${CLENG_MINGW_SYSROOT}")
else()
  message(WARNING "cleng: no mingw-w64 sysroot detected; cleng.exe will ship without standard C/C++ headers for the windows target. Set -DCLENG_MINGW_SYSROOT=/path/to/x86_64-w64-mingw32 to override.")
endif()

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
  # Bundle the mingw-w64 sysroot include tree so cleng can find <stdio.h>,
  # <vector>, <sstream> etc. when targeting x86_64-w64-mingw32 — these are
  # standard libc / libstdc++ headers, not part of the compiler. cleng
  # auto-injects the matching `-isystem` paths at runtime when it detects
  # the mingw target triple. Skipped (with a warning at configure time) if
  # CLENG_MINGW_SYSROOT was not detected.
  COMMAND ${CMAKE_COMMAND}
          -DSYSROOT_SOURCE=${CLENG_MINGW_SYSROOT}
          -DSYSROOT_DEST=${XBAX_PACKAGE_DIR}/cleng/sysroot/x86_64-w64-mingw32
          -P "${CLENG_SOURCE_DIR}/scripts/copy_sysroot.cmake"
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

add_custom_target(cleng DEPENDS "${CLENG_OUTPUT}")
set_target_properties(cleng PROPERTIES FOLDER "Apps")

add_custom_target(package-xbax ALL DEPENDS campiler dastrabution anzipper gatter sarver cliant cleng)
set_target_properties(package-xbax PROPERTIES FOLDER "Apps")

add_custom_target(host-tools ALL DEPENDS host-cliant)
set_target_properties(host-tools PROPERTIES FOLDER "Apps")
