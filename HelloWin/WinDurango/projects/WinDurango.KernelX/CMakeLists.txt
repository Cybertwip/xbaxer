cmake_minimum_required(VERSION 4.0)
project(WinDurango.KernelX VERSION 1.0.0)

set(VERSION_SUFFIX "-dev.1") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

set(FILES
    src/kernelx.cpp
    src/dllmain.cpp
    src/Hooks.cpp
    src/CurrentApp.cpp
    include/WinDurango.KernelX/CurrentApp.h
    include/WinDurango.KernelX/kernelx.h
    include/WinDurango.KernelX/Logan.h
    include/WinDurango.KernelX/Hooks.h
)

add_library(WinDurango.KernelX SHARED ${FILES})

target_link_libraries(WinDurango.KernelX PRIVATE 
    WinDurango.Common 
    WinDurango.Implementation.WinRT 
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/vcpkg/pkgs/detours_x64-windows/lib/detours.lib"
)

target_link_options(WinDurango.KernelX
    PUBLIC 
    $<$<CXX_COMPILER_ID:MSVC>:/FORCE:MULTIPLE>
)

target_include_directories(WinDurango.KernelX PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/WinDurango.KernelX/
    ${CMAKE_SOURCE_DIR}/projects/WinDurango.Common/include
    ${CMAKE_SOURCE_DIR}/projects/WinDurango.Implementation.WinRT/include
    ${CMAKE_SOURCE_DIR}/vcpkg_installed/vcpkg/pkgs/detours_x64-windows/include/detours
)

set_target_properties(WinDurango.KernelX PROPERTIES
    OUTPUT_NAME "kernelx"
)