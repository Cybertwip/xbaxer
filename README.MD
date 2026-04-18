# xbax-stream

`xbax-stream` is a small Xbox devkit streaming and remote-shell workspace.

It currently contains:

- `main.py`: a Python desktop client for live video, remote input, PIN-based dev-shell access, and file upload to the sandbox.
- `go/`: a vendored Go 1.24.13 source tree used to build a custom toolchain during the CMake build.
- `dastrabution/`: a Go command that links against `github.com/go-git/go-git/v5` and is cross-compiled as a Windows PE executable.
- `anzipper/`: a Go command for listing and safely extracting `.zip` archives.
- `gatter/`: a Go command for HTTP/HTTPS downloads, similar to a very small single-purpose fetch client.
- `Sarver/`: a Go HTTP build server that accepts Go source archives, compiles them remotely, and streams the built artifact back.
- `Cliant/`: a Go client for the remote build server, supporting commands such as `cliant <server-url> build .`.
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
- `build/package/Xbax/anzipper/anzipper.exe`
- `build/package/Xbax/gatter/gatter.exe`
- `build/package/Xbax/sarver/sarver.exe`
- `build/package/Xbax/cliant/cliant.exe`
- `build/host/cliant/cliant` on macOS/Linux hosts, or `build/host/cliant/cliant.exe` on Windows hosts

## Notes

- `campiler` is built from the vendored Go source, using the previously built host Go toolchain as `GOROOT_BOOTSTRAP`.
- `dastrabution.exe` is built with the custom Go toolchain and targets `windows/amd64`.
- `sarver` exposes an HTTP API for remote `go build` requests and automatically prefers the packaged `campiler/bin/go.exe` when it is available next to the Xbax package.
- `cliant` archives a Go workspace, uploads it to `sarver`, and saves the compiled binary locally.
- The local build also produces a native host copy of `cliant` under `build/host/cliant/`.
- Build artifacts are intentionally ignored by the repository `.gitignore`.

## Remote Build Example

Start the server from the packaged Xbax tree on the remote machine:

```bash
sarver.exe -listen 0.0.0.0:17777
```

Then build a local Go workspace through it:

```bash
cliant http://<server-ip>:17777 build . -goos windows -goarch amd64 -o myapp.exe
```

## Licensing

Project-specific files in this repository are covered by the Microsoft Public License in [LICENSE.MD](./LICENSE.MD).

Third-party code keeps its upstream license. In particular, the vendored Go source tree under `go/` retains its own license files, including `go/LICENSE`.
