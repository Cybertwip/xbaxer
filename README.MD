# xbax-stream

`xbax-stream` is a small Xbox devkit streaming and remote-shell workspace.

It currently contains:

- `main.py`: a Python desktop client for live video, remote input, PIN-based dev-shell access, and file upload to the sandbox.
- `go/`: a vendored Go 1.24.13 source tree used to build a custom toolchain during the CMake build.
- `dastrabution/`: a Go command that links against `github.com/go-git/go-git/v5` and is cross-compiled as a Windows PE executable.
- `campiler/`: packaging helpers for producing a Windows-targeted Go bootstrap/toolchain tree from the previously built host Go toolchain.

## Requirements

Python packages:

- `pygame`
- `opencv-python-headless`
- `numpy`
- `websocket-client`
- `requests`
- `paramiko`

Install them with:

```bash
python3 -m pip install -r requirements.txt
```

## Run The Stream Client

```bash
python3 main.py
```

The client scans the local network for devkits, connects to the live stream, and can open the PIN-based dev shell.

## Build The Packaged Windows Outputs

Configure once:

```bash
cmake -S . -B build
```

Build everything:

```bash
cmake --build build -j4
```

The default build packages these outputs:

- `build/package/Xbax/campiler/bin/go.exe`
- `build/package/Xbax/campiler/bin/gofmt.exe`
- `build/package/Xbax/campiler/pkg/tool/windows_amd64/...`
- `build/package/Xbax/dastrabution/dastrabution.exe`

## Notes

- `campiler` is built from the vendored Go source, using the previously built host Go toolchain as `GOROOT_BOOTSTRAP`.
- `dastrabution.exe` is built with the custom Go toolchain and targets `windows/amd64`.
- Build artifacts are intentionally ignored by the repository `.gitignore`.

## Licensing

Project-specific files in this repository are covered by the Microsoft Public License in [LICENSE.MD](./LICENSE.MD).

Third-party code keeps its upstream license. In particular, the vendored Go source tree under `go/` retains its own license files, including `go/LICENSE`.
