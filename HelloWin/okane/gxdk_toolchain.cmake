#
# gxdk_toolchain.cmake : CMake toolchain for Microsoft GDK targeting Xbox One (Gaming.Xbox.XboxOne.x64)
#
# Derived from Microsoft's official CMake GDK examples.
# Copyright (c) Microsoft Corporation. Licensed under the MIT License.
#

mark_as_advanced(CMAKE_TOOLCHAIN_FILE)

if(_POWER_GXDK_TOOLCHAIN_)
  return()
endif()

set(CMAKE_SYSTEM_NAME WINDOWS)
set(CMAKE_SYSTEM_VERSION 10.0)
set(XBOX_CONSOLE_TARGET "xboxone" CACHE STRING "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(XdkEditionTarget "251000" CACHE STRING "Microsoft GDK Edition")

message("XdkEditionTarget = ${XdkEditionTarget}")

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
    set(CMAKE_CXX_COMPILER "${_POWER_MSVC_TOOLSET_DIR}/bin/Hostx64/x64/cl.exe" CACHE FILEPATH "CXX compiler")
    set(CMAKE_C_COMPILER "${_POWER_MSVC_TOOLSET_DIR}/bin/Hostx64/x64/cl.exe" CACHE FILEPATH "C compiler")
    set(MSVC_TOOLSET_ROOT "${_POWER_MSVC_TOOLSET_DIR}" CACHE INTERNAL "MSVC toolset root directory")
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

        set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} /I\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/um\" /I\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/shared\" /I\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/ucrt\" /I\"${_POWER_WinSdkRoot}/Include/${_POWER_WinSdkVer}/winrt\"" CACHE STRING "" FORCE)

        foreach(t EXE SHARED MODULE)
            string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_WinSdkRootNative}\\Lib\\${_POWER_WinSdkVer}\\um\\x64\" /LIBPATH:\"${_POWER_WinSdkRootNative}\\Lib\\${_POWER_WinSdkVer}\\ucrt\\x64\"")
        endforeach()

        if(MSVC_TOOLSET_ROOT AND EXISTS "${MSVC_TOOLSET_ROOT}/include")
            file(TO_NATIVE_PATH "${MSVC_TOOLSET_ROOT}" _POWER_MSVC_TOOLSET_ROOT_NATIVE)
            set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} /I\"${MSVC_TOOLSET_ROOT}/include\"" CACHE STRING "" FORCE)
            foreach(t EXE SHARED MODULE)
                string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_MSVC_TOOLSET_ROOT_NATIVE}\\lib\\x64\"")
            endforeach()
        endif()
    endif()
endif()

#--- GDK SDK discovery
GET_FILENAME_COMPONENT(Console_SdkRoot "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\GDK;InstallPath]" ABSOLUTE CACHE)
_power_resolve_gdk_edition("${Console_SdkRoot}")
if(Console_SdkRoot AND XdkEditionTarget)
    if(EXISTS "${Console_SdkRoot}/${XdkEditionTarget}")
        file(TO_NATIVE_PATH "${Console_SdkRoot}" _POWER_Console_SdkRootNative)
        set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} /I\"${Console_SdkRoot}/${XdkEditionTarget}/xbox/include\" /I\"${Console_SdkRoot}/${XdkEditionTarget}/xbox/include/gen8\"" CACHE STRING "" FORCE)
        foreach(t EXE SHARED MODULE)
            string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /LIBPATH:\"${_POWER_Console_SdkRootNative}\\${XdkEditionTarget}\\xbox\\lib\\x64\" /LIBPATH:\"${_POWER_Console_SdkRootNative}\\${XdkEditionTarget}\\xbox\\lib\\gen8\"")
        endforeach()
    endif()
endif()

set(CMAKE_GENERATOR_PLATFORM "Gaming.Xbox.XboxOne.x64" CACHE STRING "" FORCE)
set(CMAKE_VS_PLATFORM_NAME "Gaming.Xbox.XboxOne.x64" CACHE STRING "" FORCE)
set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64" CACHE STRING "" FORCE)

# Let the GDK MSBuild rules decide WindowsTargetPlatformVersion
set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "" CACHE STRING "" FORCE)

# Propagate GDK version to MSBuild
set(CMAKE_VS_GLOBALS "XdkEditionTarget=${XdkEditionTarget}" CACHE STRING "" FORCE)

if(${CMAKE_VERSION} GREATER_EQUAL "3.30")
    set(CMAKE_VS_USE_DEBUG_LIBRARIES "false" CACHE STRING "" FORCE)
endif()

#--- Platform defines
set(CMAKE_CXX_FLAGS_INIT "$ENV{CFLAGS} ${CMAKE_CXX_FLAGS_INIT} -D_GAMING_XBOX -D_GAMING_XBOX_XBOXONE -DWINAPI_FAMILY=WINAPI_FAMILY_GAMES -D_ATL_NO_DEFAULT_LIBS -D__WRL_NO_DEFAULT_LIB__ -D_CRT_USE_WINAPI_PARTITION_APP -D_UITHREADCTXT_SUPPORT=0 -D__WRL_CLASSIC_COM_STRICT__ /MD /arch:AVX /favor:AMD64" CACHE STRING "" FORCE)

#--- Platform libraries
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "xgameplatform.lib" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES_INIT} CACHE STRING "" FORCE)

#--- Block unsupported desktop libraries
foreach(t EXE SHARED MODULE)
    string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB:advapi32.lib /NODEFAULTLIB:comctl32.lib /NODEFAULTLIB:comsupp.lib /NODEFAULTLIB:dbghelp.lib /NODEFAULTLIB:gdi32.lib /NODEFAULTLIB:gdiplus.lib /NODEFAULTLIB:guardcfw.lib /NODEFAULTLIB:kernel32.lib /NODEFAULTLIB:mmc.lib /NODEFAULTLIB:msimg32.lib /NODEFAULTLIB:msvcole.lib /NODEFAULTLIB:msvcoled.lib /NODEFAULTLIB:mswsock.lib /NODEFAULTLIB:ntstrsafe.lib /NODEFAULTLIB:ole2.lib /NODEFAULTLIB:ole2autd.lib /NODEFAULTLIB:ole2auto.lib /NODEFAULTLIB:ole2d.lib /NODEFAULTLIB:ole2ui.lib /NODEFAULTLIB:ole2uid.lib /NODEFAULTLIB:ole32.lib /NODEFAULTLIB:oleacc.lib /NODEFAULTLIB:oleaut32.lib /NODEFAULTLIB:oledlg.lib /NODEFAULTLIB:oledlgd.lib /NODEFAULTLIB:oldnames.lib /NODEFAULTLIB:runtimeobject.lib /NODEFAULTLIB:shell32.lib /NODEFAULTLIB:shlwapi.lib /NODEFAULTLIB:strsafe.lib /NODEFAULTLIB:urlmon.lib /NODEFAULTLIB:user32.lib /NODEFAULTLIB:userenv.lib /NODEFAULTLIB:wlmole.lib /NODEFAULTLIB:wlmoled.lib /NODEFAULTLIB:onecore.lib")
endforeach()

foreach(t EXE SHARED MODULE)
    set(_POWER_LINK_FLAGS "${CMAKE_${t}_LINKER_FLAGS}")
    if(NOT _POWER_LINK_FLAGS MATCHES "(^| )/DYNAMICBASE( |$)")
        string(APPEND _POWER_LINK_FLAGS " /DYNAMICBASE /NXCOMPAT")
        set(CMAKE_${t}_LINKER_FLAGS "${_POWER_LINK_FLAGS}" CACHE STRING "" FORCE)
    endif()
endforeach()

#--- Exported variables for project CMakeLists (from CMakeExample pattern)
set(Console_Defines _UNICODE UNICODE WIN32_LEAN_AND_MEAN _GAMING_XBOX _GAMING_XBOX_XBOXONE WINAPI_FAMILY=WINAPI_FAMILY_GAMES _CRT_USE_WINAPI_PARTITION_APP _UITHREADCTXT_SUPPORT=0 __WRL_CLASSIC_COM_STRICT__ _ATL_NO_DEFAULT_LIBS __WRL_NO_DEFAULT_LIB__)
set(Console_Libs pixevt.lib d3d12_x.lib xgameplatform.lib xgameruntime.lib xmem.lib xg_x.lib)
set(Console_ArchOptions /favor:AMD64 /arch:AVX)
set(Console_LinkOptions /DYNAMICBASE /NXCOMPAT)

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
    find_program(GDK_DXCTool NAMES dxc dxc.exe
        PATHS
            "${Console_SdkRoot}/${XdkEditionTarget}/xbox/bin/gen8"
            "${Console_SdkRoot}/bin/XboxOne/pixsc"
        NO_DEFAULT_PATH)

    if(NOT GDK_DXCTool)
        find_program(GDK_DXCTool NAMES dxc dxc.exe)
    endif()

    mark_as_advanced(GDK_DXCTool)
endif()

#--- Threads fix
set(CMAKE_USE_WIN32_THREADS_INIT 1)
set(Threads_FOUND TRUE)

set(_POWER_GXDK_TOOLCHAIN_ ON)
