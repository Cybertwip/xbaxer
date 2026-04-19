#
# grdk_toolchain.cmake : CMake toolchain for Microsoft GDK targeting Desktop (x64)
#
# Derived from Microsoft's official CMake GDK examples.
# Copyright (c) Microsoft Corporation. Licensed under the MIT License.
#

mark_as_advanced(CMAKE_TOOLCHAIN_FILE)

if(_POWER_GRDK_TOOLCHAIN_)
  return()
endif()

set(CMAKE_SYSTEM_NAME WINDOWS)
set(CMAKE_SYSTEM_VERSION 10.0)

set(XdkEditionTarget "251000" CACHE STRING "Microsoft GDK Edition")

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XdkEditionTarget BUILD_USING_BWOI)

function(_power_resolve_gdk_edition SDK_ROOT)
    if(NOT SDK_ROOT OR EXISTS "${SDK_ROOT}/${XdkEditionTarget}")
        return()
    endif()

    file(GLOB _POWER_GDK_EDITION_DIRS LIST_DIRECTORIES TRUE "${SDK_ROOT}/25*")
    set(_POWER_GDK_EDITIONS)
    foreach(_POWER_GDK_EDITION_DIR IN LISTS _POWER_GDK_EDITION_DIRS)
        get_filename_component(_POWER_GDK_EDITION "${_POWER_GDK_EDITION_DIR}" NAME)
        if(_POWER_GDK_EDITION MATCHES "^[0-9]+$")
            list(APPEND _POWER_GDK_EDITIONS "${_POWER_GDK_EDITION}")
        endif()
    endforeach()

    if(_POWER_GDK_EDITIONS)
        list(SORT _POWER_GDK_EDITIONS)
        list(GET _POWER_GDK_EDITIONS -1 _POWER_GDK_LATEST)
        message(STATUS
            "Requested GDK edition ${XdkEditionTarget} was not found under ${SDK_ROOT}; "
            "using installed edition ${_POWER_GDK_LATEST}")
        set(XdkEditionTarget "${_POWER_GDK_LATEST}" CACHE STRING "Microsoft GDK Edition" FORCE)
    endif()
endfunction()

#--- Compiler discovery
set(PROGRAM_FILES_X86 "$ENV{ProgramFiles\(x86\)}")

file(GLOB _POWER_MSVC_VERSIONS
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*"
    "$ENV{ProgramFiles}/Microsoft Visual Studio/2019/*/VC/Tools/MSVC/*"
    "${PROGRAM_FILES_X86}/Microsoft Visual Studio/2019/*/VC/Tools/MSVC/*"
)

if(_POWER_MSVC_VERSIONS)
    list(SORT _POWER_MSVC_VERSIONS)
    list(GET _POWER_MSVC_VERSIONS -1 _POWER_MSVC_TOOLSET_DIR)

    if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "ClangCL" OR CMAKE_GENERATOR_TOOLSET MATCHES "ClangCL")
        find_program(_POWER_CLANG_CL clang-cl.exe
            PATHS
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/*/VC/Tools/Llvm/x64/bin"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2019/*/VC/Tools/Llvm/x64/bin"
            NO_DEFAULT_PATH)
        if(_POWER_CLANG_CL)
            set(CMAKE_CXX_COMPILER "${_POWER_CLANG_CL}" CACHE FILEPATH "CXX compiler")
            set(CMAKE_C_COMPILER "${_POWER_CLANG_CL}" CACHE FILEPATH "C compiler")
        endif()
    else()
        set(CMAKE_CXX_COMPILER "${_POWER_MSVC_TOOLSET_DIR}/bin/Hostx64/x64/cl.exe" CACHE FILEPATH "CXX compiler")
        set(CMAKE_C_COMPILER "${_POWER_MSVC_TOOLSET_DIR}/bin/Hostx64/x64/cl.exe" CACHE FILEPATH "C compiler")
    endif()

    set(MSVC_TOOLSET_ROOT "${_POWER_MSVC_TOOLSET_DIR}" CACHE INTERNAL "MSVC toolset root directory")
endif()

set(CMAKE_GENERATOR_PLATFORM "x64" CACHE STRING "" FORCE)
set(CMAKE_VS_PLATFORM_NAME "x64" CACHE STRING "" FORCE)
set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64" CACHE STRING "" FORCE)

# Let the GDK MSBuild rules decide WindowsTargetPlatformVersion
set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "" CACHE STRING "" FORCE)

# Propagate GDK version to MSBuild
set(CMAKE_VS_GLOBALS "XdkEditionTarget=${XdkEditionTarget}" CACHE STRING "" FORCE)

if(${CMAKE_VERSION} GREATER_EQUAL "3.30")
    set(CMAKE_VS_USE_DEBUG_LIBRARIES "$<CONFIG:Debug>" CACHE STRING "" FORCE)
endif()

#--- Windows SDK discovery
if(DEFINED ENV{WindowsSdkDir})
    set(_POWER_WinSdkRoot "$ENV{WindowsSdkDir}")
elseif(DEFINED ENV{ProgramFiles\(x86\)})
    set(_POWER_WinSdkRoot "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
else()
    set(_POWER_WinSdkRoot "C:/Program Files (x86)/Windows Kits/10")
endif()

if(EXISTS "${_POWER_WinSdkRoot}")
    file(GLOB _POWER_WinSdkVersions "${_POWER_WinSdkRoot}/Include/10.0.*")
    if(_POWER_WinSdkVersions)
        list(SORT _POWER_WinSdkVersions)
        list(GET _POWER_WinSdkVersions -1 _POWER_WinSdkLatest)
        get_filename_component(_POWER_WinSdkVer ${_POWER_WinSdkLatest} NAME)
        file(TO_NATIVE_PATH "${_POWER_WinSdkRoot}" _POWER_WinSdkRootNative)

        if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "ClangCL" OR CMAKE_GENERATOR_TOOLSET MATCHES "ClangCL")
            set(_I "-I")
        else()
            set(_I "/I")
        endif()

        set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -D_GAMING_DESKTOP -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP ${_I}\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/um\" ${_I}\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/shared\" ${_I}\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/ucrt\" ${_I}\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/winrt\"" CACHE STRING "" FORCE)

        foreach(t EXE SHARED MODULE)
            string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_WinSdkRootNative}\\Lib\\${_POWER_WinSdkVer}\\um\\x64\" /LIBPATH:\"${_POWER_WinSdkRootNative}\\Lib\\${_POWER_WinSdkVer}\\ucrt\\x64\"")
        endforeach()

        if(MSVC_TOOLSET_ROOT AND EXISTS "${MSVC_TOOLSET_ROOT}/include")
            file(TO_NATIVE_PATH "${MSVC_TOOLSET_ROOT}" _POWER_MSVC_TOOLSET_ROOT_NATIVE)
            set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_I}\"${MSVC_TOOLSET_ROOT}/include\"" CACHE STRING "" FORCE)
            foreach(t EXE SHARED MODULE)
                string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_MSVC_TOOLSET_ROOT_NATIVE}\\lib\\x64\"")
            endforeach()
        endif()
    else()
        set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -D_GAMING_DESKTOP -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP" CACHE STRING "" FORCE)
    endif()
else()
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -D_GAMING_DESKTOP -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP" CACHE STRING "" FORCE)
endif()

#--- GDK SDK discovery (Desktop)
GET_FILENAME_COMPONENT(Console_SdkRoot "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\GDK;GRDKInstallPath]" ABSOLUTE CACHE)
_power_resolve_gdk_edition("${Console_SdkRoot}")
if(Console_SdkRoot AND XdkEditionTarget)
    if(EXISTS "${Console_SdkRoot}/${XdkEditionTarget}")
        file(TO_NATIVE_PATH "${Console_SdkRoot}" _POWER_Console_SdkRootNative)
        if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "ClangCL" OR CMAKE_GENERATOR_TOOLSET MATCHES "ClangCL")
            set(_I "-I")
        else()
            set(_I "/I")
        endif()

        if(XdkEditionTarget GREATER_EQUAL 251000)
            set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_I}\"${Console_SdkRoot}/${XdkEditionTarget}/windows/include\"" CACHE STRING "" FORCE)
            foreach(t EXE SHARED MODULE)
                string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_Console_SdkRootNative}\\${XdkEditionTarget}\\windows\\lib\\x64\"")
            endforeach()
        else()
            set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_I}\"${Console_SdkRoot}/${XdkEditionTarget}/GRDK/gameKit/Include\"" CACHE STRING "" FORCE)
            foreach(t EXE SHARED MODULE)
                string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_Console_SdkRootNative}\\${XdkEditionTarget}\\GRDK\\gameKit\\Lib\\amd64\"")
            endforeach()
        endif()
    endif()
endif()

# Propagate the resolved GDK version to MSBuild
set(CMAKE_VS_GLOBALS "XdkEditionTarget=${XdkEditionTarget}" CACHE STRING "" FORCE)

#--- GDK build props for Visual Studio IDE
file(GENERATE OUTPUT gdk_build.props INPUT "${CMAKE_CURRENT_LIST_DIR}/gdk_build.props")

function(add_executable target_name)
  _add_executable(${target_name} ${ARGN})
  set_target_properties(${target_name} PROPERTIES VS_USER_PROPS gdk_build.props)
endfunction()

function(add_library target_name)
  _add_library(${target_name} ${ARGN})
  set_target_properties(${target_name} PROPERTIES VS_USER_PROPS gdk_build.props)
endfunction()

#--- DXC compiler
if(NOT GDK_DXCTool)
    set(_POWER_DXC_SEARCH_PATHS)

    if(_POWER_WinSdkRoot AND _POWER_WinSdkVer)
        list(APPEND _POWER_DXC_SEARCH_PATHS
            "${_POWER_WinSdkRoot}/bin/${_POWER_WinSdkVer}/x64")
    endif()

    if(Console_SdkRoot AND XdkEditionTarget)
        list(APPEND _POWER_DXC_SEARCH_PATHS
            "${Console_SdkRoot}/${XdkEditionTarget}/windows/bin/x64")
    endif()

    find_program(GDK_DXCTool NAMES dxc dxc.exe
        PATHS ${_POWER_DXC_SEARCH_PATHS}
        NO_DEFAULT_PATH)

    if(NOT GDK_DXCTool)
        find_program(GDK_DXCTool NAMES dxc dxc.exe)
    endif()

    mark_as_advanced(GDK_DXCTool)
endif()

#--- Threads fix
set(CMAKE_USE_WIN32_THREADS_INIT 1)
set(Threads_FOUND TRUE)

set(_POWER_GRDK_TOOLCHAIN_ ON)
