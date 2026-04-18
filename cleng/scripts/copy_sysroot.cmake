# copy_sysroot.cmake — bundle the mingw-w64 sysroot include tree next to
# cleng.exe. Invoked from the top-level CMakeLists `cleng` custom command:
#
#   cmake -DSYSROOT_SOURCE=<mingw root>/x86_64-w64-mingw32 \
#         -DSYSROOT_DEST=<package>/cleng/sysroot/x86_64-w64-mingw32 \
#         -P copy_sysroot.cmake
#
# A no-op (with a status line) if SYSROOT_SOURCE is empty or missing — this
# lets the cleng build still succeed on machines without mingw installed,
# although the resulting cleng.exe will not be able to find standard C/C++
# headers for the windows target without a developer-supplied sysroot.

if(NOT DEFINED SYSROOT_SOURCE OR SYSROOT_SOURCE STREQUAL "")
  message(STATUS "cleng sysroot: SYSROOT_SOURCE not set; skipping bundle (cleng will lack <stdio.h>/<vector> for windows)")
  return()
endif()

if(NOT EXISTS "${SYSROOT_SOURCE}/include/stdio.h")
  message(STATUS "cleng sysroot: ${SYSROOT_SOURCE}/include/stdio.h not found; skipping bundle")
  return()
endif()

if(NOT DEFINED SYSROOT_DEST OR SYSROOT_DEST STREQUAL "")
  message(FATAL_ERROR "copy_sysroot.cmake: SYSROOT_DEST is required")
endif()

# Skip the (~100 MB) copy if the bundle already exists and contains the
# standard C and C++ headers we expect. Explicit cache check — delete
# ${SYSROOT_DEST} (or the whole cleng package) to force a refresh.
file(GLOB _cxx_dirs "${SYSROOT_DEST}/include/c++/*")
if(EXISTS "${SYSROOT_DEST}/include/stdio.h" AND _cxx_dirs)
  message(STATUS "cleng sysroot: ${SYSROOT_DEST}/include already populated; skipping copy")
  return()
endif()

message(STATUS "cleng sysroot: copying ${SYSROOT_SOURCE}/include -> ${SYSROOT_DEST}/include")
file(REMOVE_RECURSE "${SYSROOT_DEST}/include")
file(MAKE_DIRECTORY "${SYSROOT_DEST}/include")
file(COPY "${SYSROOT_SOURCE}/include/" DESTINATION "${SYSROOT_DEST}/include")
