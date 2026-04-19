cmake_minimum_required(VERSION 3.30)
project(WinDurango.Implementation.Native VERSION 1.0.0)

set(VERSION_SUFFIX "-dev.0") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

set(FILES
    include/WinDurango.Implementation.Native/WinDurangoNative.h
)

add_compile_definitions(WDIMPL_API_EXPORTS)

add_library(WinDurango.Implementation.Native SHARED ${FILES})
target_include_directories(WinDurango.Implementation.Native PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
)

target_link_libraries(WinDurango.Implementation.Native PRIVATE WinDurango.Common)

target_compile_definitions(WinDurango.Implementation.Native PUBLIC
        WINDURANGO_IMPLEMENTATION_NATIVE_COMPILER_NAME="${CMAKE_CXX_COMPILER_ID}"
        WINDURANGO_IMPLEMENTATION_NATIVE_PLATFORM_NAME="${CMAKE_SYSTEM_NAME}"
        WINDURANGO_IMPLEMENTATION_NATIVE_PLATFORM_ARCH="${CMAKE_SYSTEM_PROCESSOR}"
        WINDURANGO_IMPLEMENTATION_NATIVE_VERSION="${PROJECT_VERSION}${VERSION_SUFFIX}"
        WINDURANGO_IMPLEMENTATION_NATIVE_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH}
)

set_target_properties(WinDurango.Implementation.Native PROPERTIES LINKER_LANGUAGE CXX)