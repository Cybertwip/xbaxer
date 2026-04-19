cmake_minimum_required(VERSION 3.21)

project(TriangleCpp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_executable(TriangleCpp WIN32
  TriangleApp.cpp
)

target_compile_definitions(TriangleCpp PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(TriangleCpp PRIVATE
  kernel32
)

target_link_options(TriangleCpp PRIVATE
  -municode
)
