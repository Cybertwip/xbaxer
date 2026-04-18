# copy_sysroot.cmake — bundle a per-triplet target runtime tree next to
# cleng.exe. Invoked from the top-level CMakeLists `cleng` custom command:
#
#   cmake -DSYSROOT_SOURCE=<source-root> \
#         -DSYSROOT_DEST=<package>/cleng/sysroot/x86_64-w64-mingw32 \
#         -DSYSROOT_MARKER=<relative/path/that/must/exist> \
#         -DSYSROOT_COMPONENTS=<rel1;rel2;...> \
#         -P copy_sysroot.cmake
#
# SYSROOT_COMPONENTS is optional. When omitted, the entire source root is
# copied; otherwise only the listed top-level entries are mirrored. A no-op
# (with a status line) if SYSROOT_SOURCE is empty/missing or the marker path
# is absent — this lets the build still succeed on hosts that lack a given
# optional SDK/toolchain.

if(NOT DEFINED SYSROOT_SOURCE OR SYSROOT_SOURCE STREQUAL "")
  message(STATUS "cleng target bundle: SYSROOT_SOURCE not set; skipping bundle")
  return()
endif()

if(NOT DEFINED SYSROOT_MARKER OR SYSROOT_MARKER STREQUAL "")
  set(SYSROOT_MARKER "include/stdio.h")
endif()

if(NOT DEFINED SYSROOT_DEST OR SYSROOT_DEST STREQUAL "")
  message(FATAL_ERROR "copy_sysroot.cmake: SYSROOT_DEST is required")
endif()

if(NOT EXISTS "${SYSROOT_SOURCE}/${SYSROOT_MARKER}")
  message(STATUS "cleng target bundle: ${SYSROOT_SOURCE}/${SYSROOT_MARKER} not found; skipping bundle")
  return()
endif()

# Skip the copy if the destination already contains the marker we expect.
# Delete ${SYSROOT_DEST} (or the whole cleng package) to force a refresh.
if(EXISTS "${SYSROOT_DEST}/${SYSROOT_MARKER}")
  message(STATUS "cleng target bundle: ${SYSROOT_DEST} already populated; skipping copy")
  return()
endif()

message(STATUS "cleng target bundle: staging ${SYSROOT_SOURCE} -> ${SYSROOT_DEST}")
file(REMOVE_RECURSE "${SYSROOT_DEST}")
file(MAKE_DIRECTORY "${SYSROOT_DEST}")

if(DEFINED SYSROOT_COMPONENTS AND NOT SYSROOT_COMPONENTS STREQUAL "")
  foreach(_rel IN LISTS SYSROOT_COMPONENTS)
    if(EXISTS "${SYSROOT_SOURCE}/${_rel}")
      file(COPY "${SYSROOT_SOURCE}/${_rel}" DESTINATION "${SYSROOT_DEST}")
    endif()
  endforeach()
else()
  file(COPY "${SYSROOT_SOURCE}/" DESTINATION "${SYSROOT_DEST}")
endif()
