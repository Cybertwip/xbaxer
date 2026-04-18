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

rm -rf "$WORK_DIR" "$OUTPUT_DIR"
mkdir -p "$(dirname "$WORK_DIR")" "$(dirname "$OUTPUT_DIR")"

cp -Rp "$GO_SOURCE_DIR" "$WORK_DIR"
chmod -R u+w "$WORK_DIR"
chmod +x \
  "$WORK_DIR/src/make.bash" \
  "$WORK_DIR/src/all.bash" \
  "$WORK_DIR/src/bootstrap.bash"

pushd "$WORK_DIR/src" >/dev/null
GOROOT_BOOTSTRAP="$BOOTSTRAP_GOROOT" GOOS="$TARGET_GOOS" GOARCH="$TARGET_GOARCH" bash ./make.bash --no-banner

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
  rm -rf "$WORK_DIR/pkg/${gohostos}_${gohostarch}" "$WORK_DIR/pkg/tool/${gohostos}_${gohostarch}"
fi

rm -rf "$WORK_DIR/pkg/bootstrap" "$WORK_DIR/pkg/obj" "$WORK_DIR/.git"

cp -Rp "$WORK_DIR" "$OUTPUT_DIR"
rm -rf "$WORK_DIR"
