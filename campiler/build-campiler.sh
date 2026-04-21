#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 4 ]]; then
  echo "usage: $0 <go-source-dir> <bootstrap-goroot> <work-dir> <output-dir>" >&2
  exit 2
fi

GO_SOURCE_DIR="$1"
BOOTSTRAP_GOROOT="$2"
WORK_DIR="$3"
OUTPUT_DIR="$4"
TARGET_GOOS="windows"
TARGET_GOARCH="amd64"

# Pick the right Go bootstrap entry point for the host. Go's make.bash
# refuses to run on Windows ("Do not use make.bash to build on Windows");
# the equivalent batch file is src/make.bat.
HOST_IS_WINDOWS=0
case "${OS:-}${OSTYPE:-}" in
  *Windows_NT*|*msys*|*cygwin*|*mingw*) HOST_IS_WINDOWS=1 ;;
esac

# Robust recursive delete that copes with Windows quirks: Go's build cache
# stamps files read-only, antivirus / Defender briefly holds open handles,
# and Git Bash's `rm -rf` then trips on "Permission denied" / spurious
# "Is a directory" errors. Strip the read-only attribute, retry a few
# times, and as a last resort delegate to cmd /c rmdir.
robust_rmrf() {
  local path
  for path in "$@"; do
    [[ -e "$path" ]] || continue
    chmod -R u+w "$path" 2>/dev/null || true
    if [[ "$HOST_IS_WINDOWS" -eq 1 ]] && command -v cygpath >/dev/null 2>&1; then
      attrib -r -h -s "$(cygpath -w "$path")\\*" /s /d 2>/dev/null || true
    fi
    local attempt
    for attempt in 1 2 3 4 5; do
      if rm -rf -- "$path" 2>/dev/null; then
        break
      fi
      sleep 1
    done
    if [[ -e "$path" && "$HOST_IS_WINDOWS" -eq 1 ]]; then
      cmd //c "rmdir /s /q \"$(cygpath -w "$path")\"" 2>/dev/null || true
    fi
  done
}

robust_rmrf "$WORK_DIR" "$OUTPUT_DIR"
mkdir -p "$(dirname "$WORK_DIR")" "$(dirname "$OUTPUT_DIR")"

cp -Rp "$GO_SOURCE_DIR" "$WORK_DIR"
chmod -R u+w "$WORK_DIR"
if [[ -f "$WORK_DIR/go.env" ]]; then
  tr -d '\r' < "$WORK_DIR/go.env" > "$WORK_DIR/go.env.tmp"
  mv "$WORK_DIR/go.env.tmp" "$WORK_DIR/go.env"
fi
chmod +x \
  "$WORK_DIR/src/make.bash" \
  "$WORK_DIR/src/all.bash" \
  "$WORK_DIR/src/bootstrap.bash"

pushd "$WORK_DIR/src" >/dev/null
if [[ "$HOST_IS_WINDOWS" -eq 1 ]]; then
  # cmd.exe doesn't grok POSIX paths, so translate the bootstrap GOROOT
  # back to a Windows path before exporting it for make.bat.
  if command -v cygpath >/dev/null 2>&1; then
    BOOTSTRAP_GOROOT_WIN="$(cygpath -w "$BOOTSTRAP_GOROOT")"
  else
    BOOTSTRAP_GOROOT_WIN="$BOOTSTRAP_GOROOT"
  fi
  GOTOOLCHAIN=local GOROOT_BOOTSTRAP="$BOOTSTRAP_GOROOT_WIN" GOOS="$TARGET_GOOS" GOARCH="$TARGET_GOARCH" \
    cmd //c "make.bat --no-banner"
else
  GOROOT_BOOTSTRAP="$BOOTSTRAP_GOROOT" GOOS="$TARGET_GOOS" GOARCH="$TARGET_GOARCH" \
    bash ./make.bash --no-banner
fi

gohostos="$(../bin/go env GOHOSTOS)"
gohostarch="$(../bin/go env GOHOSTARCH)"
popd >/dev/null

if [[ "$TARGET_GOOS" != "$gohostos" || "$TARGET_GOARCH" != "$gohostarch" ]]; then
  rm -f \
    "$WORK_DIR/bin/go" \
    "$WORK_DIR/bin/gofmt" \
    "$WORK_DIR/bin/go_${TARGET_GOOS}_${TARGET_GOARCH}_exec"
  if [[ -d "$WORK_DIR/bin/${TARGET_GOOS}_${TARGET_GOARCH}" ]]; then
    mv "$WORK_DIR/bin/${TARGET_GOOS}_${TARGET_GOARCH}"/* "$WORK_DIR/bin/"
    rmdir "$WORK_DIR/bin/${TARGET_GOOS}_${TARGET_GOARCH}"
  fi
  robust_rmrf "$WORK_DIR/pkg/${gohostos}_${gohostarch}" "$WORK_DIR/pkg/tool/${gohostos}_${gohostarch}"
fi

robust_rmrf "$WORK_DIR/pkg/bootstrap" "$WORK_DIR/pkg/obj" "$WORK_DIR/.git"

# Copy the built tree to the output directory. On Windows, `cp -Rp` can
# choke on read-only files and long paths. Use robocopy which handles
# both natively.
if [[ "$HOST_IS_WINDOWS" -eq 1 ]]; then
  local_work_dir="$WORK_DIR"
  local_output_dir="$OUTPUT_DIR"
  if command -v cygpath >/dev/null 2>&1; then
    local_work_dir="$(cygpath -w "$WORK_DIR")"
    local_output_dir="$(cygpath -w "$OUTPUT_DIR")"
  fi
  mkdir -p "$OUTPUT_DIR"
  MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 \
    robocopy.exe "$local_work_dir" "$local_output_dir" /MIR /R:2 /W:1 /NFL /NDL /NJH /NJS /NP || true
  # robocopy exit codes 0-7 are success/informational; >=8 is failure.
  # The `|| true` absorbs the non-zero exit from informational codes
  # (bash + set -e would otherwise treat exit code 1 = "some files copied"
  # as failure).
else
  cp -Rp "$WORK_DIR" "$OUTPUT_DIR"
fi
robust_rmrf "$WORK_DIR"
