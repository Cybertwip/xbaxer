set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang CACHE STRING "")
set(CMAKE_CXX_COMPILER clang++ CACHE STRING "")
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32 CACHE STRING "")
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32 CACHE STRING "")

set(CMAKE_C_COMPILER_FORCED TRUE CACHE BOOL "" FORCE)
set(CMAKE_CXX_COMPILER_FORCED TRUE CACHE BOOL "" FORCE)
set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)
set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Keep the locally generated Ninja graph simple. The real compile/link work is
# replayed remotely through cliant -> sarver -> cleng, so configure only needs
# to describe the graph and target triple.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)
