cmake_minimum_required(VERSION 4.0)
project(WinDurango.etwplus VERSION 1.0.0)

set(VERSION_SUFFIX "-dev.1") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

set(FILES
    src/etwplus.cpp
    include/WinDurango.etwplus/etwplus.h
)

add_library(WinDurango.etwplus SHARED ${FILES})

target_link_libraries(WinDurango.etwplus PRIVATE WinDurango.Common)

target_include_directories(WinDurango.etwplus PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/WinDurango.etwplus/
    ../WinDurango.Common/include/
)

set_target_properties(WinDurango.etwplus PROPERTIES
    OUTPUT_NAME "etwplus"
)