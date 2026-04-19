cmake_minimum_required(VERSION 3.28)

cmake_policy(SET CMP0091 NEW)

project(OkaneizerReference
    DESCRIPTION "C++/HLSL Xbox-ready reference app"
    LANGUAGES C CXX)

option(BUILD_USING_BWOI     "Enable Build With/Out Install"     OFF)
option(ENABLE_CODE_ANALYSIS "Use Static Code Analysis on build" OFF)

set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)

include(${CMAKE_CURRENT_SOURCE_DIR}/gxdk_toolchain.cmake)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    if(DEFINED XBOX_CONSOLE_TARGET)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL FALSE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Gaming.Xbox.XboxOne.x64")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/Gaming.Xbox.XboxOne.x64")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Gaming.Xbox.XboxOne.x64")

set(GAME_SOURCES
    DeviceResources.cpp
    DeviceResources.h
    Game.cpp
    Game.h
    Main.cpp
    OkaneGpuHasher.cpp
    OkaneGpuHasher.h
    OkaneGpuBridge.cpp
    OkaneMiner.cpp
    OkaneMiner.h
    StepTimer.h
    pch.h
    include/OkaneBridge.h
    MicrosoftGameConfig.mgc
    # Stub implementations of desktop Win32 APIs not present on Xbox Game Core.
    XboxConsoleStubs.cpp
)

set(GAME_ASSETS
    Assets/LargeLogo.png
    Assets/Logo.png
    Assets/SmallLogo.png
    Assets/SplashScreen.png
    Assets/StoreLogo.png
)

source_group("Assets" FILES ${GAME_ASSETS})
source_group("Bridge" FILES OkaneGpuBridge.cpp include/OkaneBridge.h)
source_group("GPU" FILES OkaneGpuHasher.cpp OkaneGpuHasher.h SHA256Compute.hlsl)
source_group("Miner" FILES OkaneMiner.cpp OkaneMiner.h)

set_source_files_properties(MicrosoftGameConfig.mgc PROPERTIES VS_TOOL_OVERRIDE "MGCCompile")
set_source_files_properties(Assets/LargeLogo.png PROPERTIES VS_TOOL_OVERRIDE "CopyFileToFolders")
set_source_files_properties(Assets/Logo.png PROPERTIES VS_TOOL_OVERRIDE "CopyFileToFolders")
set_source_files_properties(Assets/SmallLogo.png PROPERTIES VS_TOOL_OVERRIDE "CopyFileToFolders")
set_source_files_properties(Assets/SplashScreen.png PROPERTIES VS_TOOL_OVERRIDE "CopyFileToFolders")
set_source_files_properties(Assets/StoreLogo.png PROPERTIES VS_TOOL_OVERRIDE "CopyFileToFolders")

add_custom_target(shaders)

set_source_files_properties(VertexShader.hlsl PROPERTIES ShaderType "vs")
set_source_files_properties(PixelShader.hlsl PROPERTIES ShaderType "ps")
set_source_files_properties(SHA256Compute.hlsl PROPERTIES ShaderType "cs")

foreach(FILE VertexShader.hlsl PixelShader.hlsl SHA256Compute.hlsl)
    get_filename_component(FILE_WE ${FILE} NAME_WE)
    get_source_file_property(shadertype ${FILE} ShaderType)
    add_custom_command(TARGET shaders POST_BUILD
        COMMAND "${GDK_DXCTool}" /nologo /Emain /T${shadertype}_6_0 $<$<CONFIG:Debug>:/Od> /Zi
            /Fo ${CMAKE_CURRENT_BINARY_DIR}/${FILE_WE}.cso
            /Fd ${CMAKE_BINARY_DIR}/${FILE_WE}.pdb
            ${FILE}
        COMMENT "HLSL ${FILE}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()

add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${GAME_ASSETS}
)

if(DEFINED XBOX_CONSOLE_TARGET)
    # Game Core on Xbox does not provide the desktop debug CRT/STL variants.
    # Force the retail DLL runtime even for Debug builds and disable iterator
    # debugging so the compiler does not request libcpmtd.lib.
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(${PROJECT_NAME} PRIVATE /MD)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
        $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_dependencies(${PROJECT_NAME} shaders)

# Link D3D12/DXGI for the GPU compute hasher (desktop) and Xbox platform libs.
if(NOT DEFINED XBOX_CONSOLE_TARGET)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d12.lib dxgi.lib)
endif()

# Link WinHTTP, bcrypt, and Winsock for the C++ miner (available on both desktop and Xbox GDK).
# Use target_link_options to pass .lib names verbatim to link.exe (same reason
# as the Xbox platform libs below).
target_link_options(${PROJECT_NAME} PRIVATE winhttp.lib bcrypt.lib ws2_32.lib)

# Link required Xbox platform libraries.
# Use target_link_options rather than target_link_libraries so CMake passes the
# .lib names verbatim to link.exe (avoiding the spurious -l prefix that CMake
# adds when linking bare names in a cross-compile configuration).
if(DEFINED XBOX_CONSOLE_TARGET)
    if(XBOX_CONSOLE_TARGET STREQUAL "scarlett")
        target_link_options(${PROJECT_NAME} PRIVATE
            pixevt.lib d3d12_xs.lib xgameruntime.lib xmem.lib xg_xs.lib)
    else()
        target_link_options(${PROJECT_NAME} PRIVATE
            pixevt.lib d3d12_x.lib xgameruntime.lib xmem.lib xg_x.lib)
    endif()
endif()

set(_okane_config_definitions
    $<$<CONFIG:Debug>:OKANE_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(NOT DEFINED XBOX_CONSOLE_TARGET)
    list(APPEND _okane_config_definitions $<$<CONFIG:Debug>:_DEBUG>)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${_okane_config_definitions}
    ${Console_Defines}
    _CRT_SECURE_NO_WARNINGS
    _UNICODE
    UNICODE
)

set(_okane_is_msvc_like FALSE)
set(_okane_compiler_probe "")
if(CMAKE_CXX_COMPILER)
    set(_okane_compiler_probe "${CMAKE_CXX_COMPILER}")
elseif(CMAKE_C_COMPILER)
    set(_okane_compiler_probe "${CMAKE_C_COMPILER}")
endif()

set(_okane_compiler_name "")
if(_okane_compiler_probe)
    get_filename_component(_okane_compiler_name "${_okane_compiler_probe}" NAME)
endif()

if(MSVC)
    set(_okane_is_msvc_like TRUE)
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_okane_is_msvc_like TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(_okane_is_msvc_like TRUE)
elseif(_okane_compiler_name MATCHES "^(cl|clang-cl)(\\.exe)?$")
    set(_okane_is_msvc_like TRUE)
endif()

if(NOT _okane_is_msvc_like)
    message(FATAL_ERROR "ERROR: This CMake file requires MSVC or an MSVC-like compiler.")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE /W4 /EHsc /fp:fast /GS /Gy /utf-8 /bigobj)
target_compile_options(${PROJECT_NAME} PRIVATE ${Console_ArchOptions})
target_link_options(${PROJECT_NAME} PRIVATE ${Console_LinkOptions})

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /Zc:inline)

    if(ENABLE_CODE_ANALYSIS)
        target_compile_options(${PROJECT_NAME} PRIVATE /analyze)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.10)
        target_compile_options(${PROJECT_NAME} PRIVATE /permissive-)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.14)
        target_compile_options(${PROJECT_NAME} PRIVATE /Zc:__cplusplus)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.26)
        target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /wd5105)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.28)
        target_compile_options(${PROJECT_NAME} PRIVATE /Zc:lambda)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-gnu-anonymous-struct
        -Wno-language-extension-token
        -Wno-nested-anon-types
        -Wno-reserved-id-macro
        -Wno-unknown-pragmas
    )
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)
        target_compile_options(${PROJECT_NAME} PRIVATE -Wno-reserved-identifier)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.0)
        target_compile_options(${PROJECT_NAME} PRIVATE -Wno-unsafe-buffer-usage)
    endif()
    target_link_options(${PROJECT_NAME} PRIVATE /IGNORE:4078)
endif()

if((MSVC_VERSION GREATER_EQUAL 1924)
   AND ((NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        OR (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.0)))
    target_compile_options(${PROJECT_NAME} PRIVATE /ZH:SHA_256)
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/VertexShader.cso
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/PixelShader.cso
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/SHA256Compute.cso
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/okane.csv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_SOLUTION_DEPLOY ON)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Required Xbox GDK MSBuild properties
if(DEFINED XBOX_CONSOLE_TARGET)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_XdkEditionTarget ${XdkEditionTarget})
    # Use OneCore CRT so the Xbox-compatible runtime is selected (avoids desktop-only
    # debug symbols like _CrtDbgReport, _malloc_dbg, _free_dbg, etc.)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_VC_Target_Library_Platform "OneCore")
endif()

if((${CMAKE_VERSION} LESS "3.30") AND (CMAKE_CONFIGURATION_TYPES MATCHES "Debug"))
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_UseDebugLibraries "true")
    set_target_properties(${PROJECT_NAME} PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_LIST_DIR}/BasicChecksWorkaround.props")
endif()

set(TDF_NAME "XboxOne")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/MicrosoftGameConfig.mgc MicrosoftGame.config @ONLY)

add_custom_target(mgccompile)
add_custom_command(TARGET mgccompile POST_BUILD
    COMMAND ${MAKEPKG_TOOL} localize
        /d "${CMAKE_BINARY_DIR}"
        /pd "${CMAKE_BINARY_DIR}/bin/Gaming.Xbox.XboxOne.x64"
    COMMENT "makepkg localize MicrosoftGame.config"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM)
