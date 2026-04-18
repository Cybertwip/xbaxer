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

add_xbax_windows_go_tool(dastrabution)
add_xbax_windows_go_tool(anzipper)
add_xbax_windows_go_tool(gatter)
add_xbax_windows_go_tool(sarver Sarver)
add_xbax_windows_go_tool(cliant Cliant)

add_custom_target(package-xbax ALL DEPENDS campiler dastrabution anzipper gatter sarver cliant)
set_target_properties(package-xbax PROPERTIES FOLDER "Apps")
