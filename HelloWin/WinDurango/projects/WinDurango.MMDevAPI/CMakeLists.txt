cmake_minimum_required(VERSION 4.0)
project(WinDurango.MMDevAPI VERSION 1.0.0)

set(VERSION_SUFFIX "-dev.1") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

set(FILES
    include/WinDurango.MMDevAPI/WinDurango.MMDevAPI.h
    src/WinDurango.MMDevAPI.cpp
    Exports.def
)

add_library(WinDurango.MMDevAPI SHARED ${FILES})

target_link_libraries(WinDurango.MMDevAPI PRIVATE WinDurango.Common)

target_include_directories(WinDurango.MMDevAPI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/WinDurango.MMDevAPI/
    ../WinDurango.Common/include/
)

target_link_options(WinDurango.MMDevAPI
    PUBLIC 
    $<$<CXX_COMPILER_ID:MSVC>:/FORCE:MULTIPLE>
)

set_target_properties(WinDurango.MMDevAPI PROPERTIES
    OUTPUT_NAME "mmdevapi"
)
