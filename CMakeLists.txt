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

set(DASTRABUTION_SOURCE_DIR "${CMAKE_SOURCE_DIR}/dastrabution")
set(DASTRABUTION_OUTPUT_DIR "${XBAX_PACKAGE_DIR}/dastrabution")
set(DASTRABUTION_OUTPUT "${DASTRABUTION_OUTPUT_DIR}/dastrabution.exe")

set(DASTRABUTION_GO_ENV
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
  OUTPUT "${DASTRABUTION_OUTPUT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-mod-cache"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/go-path"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DASTRABUTION_OUTPUT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E env ${DASTRABUTION_GO_ENV}
          "${GO_EXECUTABLE}" build -buildvcs=false -trimpath
          -o "${DASTRABUTION_OUTPUT}" .
  DEPENDS
    go-toolchain
    "${DASTRABUTION_SOURCE_DIR}/go.mod"
    "${DASTRABUTION_SOURCE_DIR}/go.sum"
    "${DASTRABUTION_SOURCE_DIR}/main.go"
  WORKING_DIRECTORY "${DASTRABUTION_SOURCE_DIR}"
  COMMENT "Building dastrabution.exe with the custom Go toolchain"
  VERBATIM
)

add_custom_target(dastrabution DEPENDS "${DASTRABUTION_OUTPUT}")
set_target_properties(dastrabution PROPERTIES FOLDER "Apps")

add_custom_target(package-xbax ALL DEPENDS campiler dastrabution)
set_target_properties(package-xbax PROPERTIES FOLDER "Apps")
