cmake_minimum_required(VERSION 3.21)
project(BuildGo LANGUAGES NONE)

# setup_go_target()
#
# Creates a build-time target "go-toolchain" that compiles Go 1.24.13
# from the vendored source tree next to this file.  The source is
# copied into ${CMAKE_BINARY_DIR}/go-toolchain/ at configure time
# (cheap); the actual compilation happens at build time so it doesn't
# block configure and participates in the dependency graph.
#
# Sets in the caller's scope:
#   GO_EXECUTABLE  – path to the built go binary  (use only in
#                    add_custom_command / add_custom_target COMMAND lines
#                    that depend on go-toolchain)
#   GO_ROOT_DIR    – the GOROOT of the built toolchain
#
# Provides the target:
#   go-toolchain   – anything that needs the Go binary should
#                    add_dependencies(myTarget go-toolchain)
function(setup_go_target)
  set(GO_SRC_DIR   "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")   # vendored source
  set(GO_BUILD_DIR "${CMAKE_BINARY_DIR}/engine/go-toolchain") # out-of-tree copy

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(GO_EXE "${GO_BUILD_DIR}/bin/go.exe")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(GO_EXE "${GO_BUILD_DIR}/bin/go")
  else()
    message(FATAL_ERROR "BuildGo: Unsupported host system: ${CMAKE_HOST_SYSTEM_NAME}")
  endif()

  # ---- copy source tree into the build directory (configure time) --------
  # Use the PowerShell-based incremental manifest sync on Windows hosts only.
  # On macOS/Linux, prefer native CMake copy logic here to avoid launching a
  # separate PowerShell terminal during configure in VS Code.
  set(_GO_SYNC_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/sync_directory.ps1")
  set(_GO_SYNC_MANIFEST "${GO_BUILD_DIR}/.sync-manifest")
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    find_program(_GO_PWSH NAMES pwsh powershell)
  else()
    unset(_GO_PWSH CACHE)
    set(_GO_PWSH "")
  endif()
  if(_GO_PWSH AND EXISTS "${_GO_SYNC_SCRIPT}")
    message(STATUS "BuildGo: Refreshing source tree via incremental manifest sync ...")
    execute_process(
      COMMAND "${_GO_PWSH}" -NoProfile -ExecutionPolicy Bypass
              -File "${_GO_SYNC_SCRIPT}"
              -Source "${GO_SRC_DIR}"
              -Dest "${GO_BUILD_DIR}"
              -Manifest "${_GO_SYNC_MANIFEST}"
      RESULT_VARIABLE GO_COPY_RESULT
    )
  else()
    message(STATUS "BuildGo: Refreshing source tree in ${GO_BUILD_DIR} (fallback) ...")
    file(MAKE_DIRECTORY "${GO_BUILD_DIR}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${GO_SRC_DIR}" "${GO_BUILD_DIR}"
      RESULT_VARIABLE GO_COPY_RESULT
    )
  endif()
  if(NOT GO_COPY_RESULT EQUAL 0)
    message(FATAL_ERROR "BuildGo: Failed to refresh source tree in ${GO_BUILD_DIR}")
  endif()

  # ---- locate a bootstrap Go (configure time) ---------------------------
  if(NOT DEFINED GOROOT_BOOTSTRAP)
    find_program(_GO_BOOTSTRAP_EXE go)
    if(_GO_BOOTSTRAP_EXE)
      get_filename_component(_GO_BOOTSTRAP_EXE "${_GO_BOOTSTRAP_EXE}" REALPATH)
      get_filename_component(_GO_BOOTSTRAP_BIN "${_GO_BOOTSTRAP_EXE}" DIRECTORY)
      get_filename_component(GOROOT_BOOTSTRAP  "${_GO_BOOTSTRAP_BIN}" DIRECTORY)
      message(STATUS "BuildGo: Auto-detected GOROOT_BOOTSTRAP=${GOROOT_BOOTSTRAP}")
    endif()
    unset(_GO_BOOTSTRAP_EXE)
    unset(_GO_BOOTSTRAP_BIN)
  endif()

  # ---- build command (platform-specific) --------------------------------
  set(BOOTSTRAP_ENV_ARGS)
  if(DEFINED GOROOT_BOOTSTRAP)
    set(BOOTSTRAP_ENV_ARGS "GOROOT_BOOTSTRAP=${GOROOT_BOOTSTRAP}")
  endif()

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(MAKE_SCRIPT "${GO_BUILD_DIR}/src/make.bat")

    if(DEFINED ENV{ComSpec} AND EXISTS "$ENV{ComSpec}")
      set(CMD_EXE "$ENV{ComSpec}")
    else()
      find_program(CMD_EXE cmd.exe REQUIRED)
    endif()

    # Generate a wrapper batch file that sets environment variables
    # internally. MSBuild mangles semicolons in env vars passed via
    # cmake -E env (splits PATH on ;), so we must set them inside
    # a .bat where cmd.exe handles them natively.
    # A stamp file gates the build: only written after make.bat succeeds,
    # so a partial/failed build is always retried.
    set(GO_BUILD_STAMP "${GO_BUILD_DIR}/.go-toolchain-built")
    set(GO_BUILD_WRAPPER "${GO_BUILD_DIR}/build-go-toolchain.bat")
    set(_wrapper_lines "@echo off\n")
    string(APPEND _wrapper_lines "if exist \"${GO_BUILD_STAMP}\" (\n")
    string(APPEND _wrapper_lines "  echo BuildGo: Toolchain already built, skipping.\n")
    string(APPEND _wrapper_lines "  exit /b 0\n")
    string(APPEND _wrapper_lines ")\n")
    if(DEFINED GOROOT_BOOTSTRAP)
      string(APPEND _wrapper_lines "set \"GOROOT_BOOTSTRAP=${GOROOT_BOOTSTRAP}\"\n")
    endif()
    foreach(_envpair IN LISTS GO_TOOLCHAIN_EXTRA_ENV)
      string(APPEND _wrapper_lines "set \"${_envpair}\"\n")
    endforeach()
    string(APPEND _wrapper_lines "call \"${MAKE_SCRIPT}\"\n")
    string(APPEND _wrapper_lines "if %errorlevel% neq 0 exit /b %errorlevel%\n")
    string(APPEND _wrapper_lines "echo.> \"${GO_BUILD_STAMP}\"\n")
    file(CONFIGURE
      OUTPUT "${GO_BUILD_WRAPPER}"
      CONTENT "${_wrapper_lines}"
      NEWLINE_STYLE WIN32
    )

    set(GO_BUILD_COMMAND "${CMD_EXE}" /c "${GO_BUILD_WRAPPER}")
    set(GO_BUILD_STAMP_COMMAND)
  else()
    set(GO_BUILD_STAMP "${GO_BUILD_DIR}/.go-toolchain-built")
    set(GO_BUILD_COMMAND
      ${CMAKE_COMMAND} -E env ${BOOTSTRAP_ENV_ARGS} ${GO_TOOLCHAIN_EXTRA_ENV}
      bash "${GO_BUILD_DIR}/src/make.bash")
    set(GO_BUILD_STAMP_COMMAND
      COMMAND ${CMAKE_COMMAND} -E touch "${GO_BUILD_STAMP}")
  endif()

  # ---- build-time custom command + target -------------------------------
  add_custom_command(
    OUTPUT  "${GO_EXE}"
    COMMAND ${GO_BUILD_COMMAND}
    ${GO_BUILD_STAMP_COMMAND}
    DEPENDS "${GO_BUILD_DIR}/src/make.bash"
    WORKING_DIRECTORY "${GO_BUILD_DIR}/src"
    COMMENT "BuildGo: Compiling Go toolchain from source ..."
    VERBATIM
  )

  add_custom_target(go-toolchain DEPENDS "${GO_EXE}")

  # ---- export paths to the caller's scope --------------------------------
  set(GO_EXECUTABLE "${GO_EXE}"       PARENT_SCOPE)
  set(GO_ROOT_DIR   "${GO_BUILD_DIR}" PARENT_SCOPE)
endfunction()
