cmake_minimum_required(VERSION 4.0)
project(WinDurango.D3D11X VERSION 1.0.0)

set(VERSION_SUFFIX "-dev.1") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

set(FILES
    src/d3d11.x.cpp
    src/IGraphicsUnknown.cpp
    src/IIDExports.cpp
    include/WinDurango.D3D11X/d3d11.x.h
    include/WinDurango.D3D11X/unknown.g.h
    include/WinDurango.D3D11X/IGraphicsUnknown.h
    include/WinDurango.D3D11X/IIDExports.h
    Exports.def
)

add_library(WinDurango.D3D11X SHARED ${FILES})

target_link_libraries(WinDurango.D3D11X PRIVATE WinDurango.Common)

target_include_directories(WinDurango.D3D11X PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/WinDurango.D3D11X/
    ../WinDurango.Common/include/
)

target_link_options(WinDurango.D3D11X
    PUBLIC 
    $<$<CXX_COMPILER_ID:MSVC>:/FORCE:MULTIPLE>
)

set_target_properties(WinDurango.D3D11X PROPERTIES
    OUTPUT_NAME "d3d11_x"
)
