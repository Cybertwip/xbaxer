cmake_minimum_required(VERSION 3.21)

project(TriangleWinDurango LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_library(kernelx SHARED
    windurango/kernelx.cpp
    windurango/kernelx.h
)
set_target_properties(kernelx PROPERTIES OUTPUT_NAME "kernelx")
set_target_properties(kernelx PROPERTIES PREFIX "")
target_compile_definitions(kernelx PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(d3d11_x SHARED
    windurango/d3d11_x.cpp
)
set_target_properties(d3d11_x PROPERTIES OUTPUT_NAME "d3d11_x")
set_target_properties(d3d11_x PROPERTIES PREFIX "")
target_compile_definitions(d3d11_x PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(TriangleWinDurango WIN32
    TriangleApp.cpp
    windurango/kernelx.h
)
target_include_directories(TriangleWinDurango PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/windurango")
target_compile_definitions(TriangleWinDurango PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _GAMING_DESKTOP)
target_link_libraries(TriangleWinDurango PRIVATE
    d3d11_x
    kernelx
    user32
    gdi32
)
target_link_options(TriangleWinDurango PRIVATE -municode)
