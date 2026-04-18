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

# Skip the copy if the destination already contains the marker we expect and
# the caller explicitly asked for reuse.
if(DEFINED SYSROOT_SKIP_IF_PRESENT AND SYSROOT_SKIP_IF_PRESENT AND EXISTS "${SYSROOT_DEST}/${SYSROOT_MARKER}")
  message(STATUS "cleng target bundle: ${SYSROOT_DEST} already populated; skipping copy")
  return()
endif()

message(STATUS "cleng target bundle: staging ${SYSROOT_SOURCE} -> ${SYSROOT_DEST}")
if(NOT DEFINED SYSROOT_PRESERVE_DEST OR NOT SYSROOT_PRESERVE_DEST)
  file(REMOVE_RECURSE "${SYSROOT_DEST}")
endif()
file(MAKE_DIRECTORY "${SYSROOT_DEST}")

function(cleng_copy_resolved_tree source_root rel_path dest_root)
  if(rel_path STREQUAL "")
    set(_source_path "${source_root}")
    set(_dest_path "${dest_root}")
  else()
    set(_source_path "${source_root}/${rel_path}")
    set(_dest_path "${dest_root}/${rel_path}")
  endif()

  if(NOT EXISTS "${_source_path}")
    return()
  endif()

  file(REAL_PATH "${_source_path}" _resolved_source)
  if(IS_DIRECTORY "${_resolved_source}")
    file(MAKE_DIRECTORY "${_dest_path}")
    file(GLOB _children RELATIVE "${_source_path}" "${_source_path}/*")
    foreach(_child IN LISTS _children)
      if(rel_path STREQUAL "")
        set(_child_rel "${_child}")
      else()
        set(_child_rel "${rel_path}/${_child}")
      endif()
      cleng_copy_resolved_tree("${source_root}" "${_child_rel}" "${dest_root}")
    endforeach()
    return()
  endif()

  get_filename_component(_dest_dir "${_dest_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dest_dir}")
  file(COPY_FILE "${_resolved_source}" "${_dest_path}" ONLY_IF_DIFFERENT)
endfunction()

# Apple SDKs and GNU sysroots often expose public headers through symlink
# chains (for example Tcl headers under the macOS SDK). Flatten those links
# while staging so the packaged bundle contains concrete files only.
if(DEFINED SYSROOT_COMPONENTS AND NOT SYSROOT_COMPONENTS STREQUAL "")
  foreach(_rel IN LISTS SYSROOT_COMPONENTS)
    cleng_copy_resolved_tree("${SYSROOT_SOURCE}" "${_rel}" "${SYSROOT_DEST}")
  endforeach()
else()
  file(GLOB _sysroot_entries RELATIVE "${SYSROOT_SOURCE}" "${SYSROOT_SOURCE}/*")
  foreach(_rel IN LISTS _sysroot_entries)
    cleng_copy_resolved_tree("${SYSROOT_SOURCE}" "${_rel}" "${SYSROOT_DEST}")
  endforeach()
endif()
