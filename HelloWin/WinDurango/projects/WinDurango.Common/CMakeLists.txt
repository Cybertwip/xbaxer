cmake_minimum_required(VERSION 4.0)
project(WinDurango.Common VERSION 1.0.0)

include(FetchContent)

set(VERSION_SUFFIX "-dev.5") # used for non-stable versions, otherwise blank
set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(WD_API_EXPORTS)

set(FILES
    include/WinDurango.Common/WinDurango.h
    include/WinDurango.Common/Config.h
    include/WinDurango.Common/Logging.h
    include/WinDurango.Common/Interfaces/Storage/Directory.h
    include/WinDurango.Common/Interfaces/Storage/File.h
    src/WinDurango.cpp
    src/Config.cpp
    src/Logging.cpp
)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/Cherrytree56567/spdlog.git GIT_TAG v1.x)

FetchContent_MakeAvailable(json)
FetchContent_MakeAvailable(spdlog)

add_library(WinDurango.Common SHARED ${FILES})
target_include_directories(WinDurango.Common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/WinDurango.Common/>
)

target_link_libraries(WinDurango.Common PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog)

target_compile_definitions(WinDurango.Common PUBLIC
    WINDURANGO_COMMON_COMPILER_NAME="${CMAKE_CXX_COMPILER_ID}"
    WINDURANGO_COMMON_PLATFORM_NAME="${CMAKE_SYSTEM_NAME}"
    WINDURANGO_COMMON_PLATFORM_ARCH="${CMAKE_SYSTEM_PROCESSOR}"
    WINDURANGO_COMMON_VERSION="${PROJECT_VERSION}${VERSION_SUFFIX}"
    WINDURANGO_COMMON_RC_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH}
)

set_target_properties(WinDurango.Common PROPERTIES LINKER_LANGUAGE CXX)