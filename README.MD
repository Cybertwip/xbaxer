# xbax-stream

`xbax-stream` is a small Xbox devkit streaming and remote-shell workspace.

It currently contains:

- `main.py`: a Python desktop client for live video, remote input, PIN-based dev-shell access, and file upload to the sandbox.
- `go/`: a vendored Go 1.24.13 source tree used to build a custom toolchain during the CMake build.
- `dastrabution/`: a Go command that links against `github.com/go-git/go-git/v5` and is cross-compiled as a Windows PE executable.
- `anzipper/`: a Go command for listing and safely extracting `.zip` archives.
- `gatter/`: a Go command for HTTP/HTTPS downloads, similar to a very small single-purpose fetch client.
- `Sarver/`: a Go HTTP build server that compiles Go source archives into binaries. In relay mode it dials out to a `cliant serve` host with `-reverse <relay-url>`, polls for queued build jobs, and streams the built artifact back.
- `Cliant/`: a Go client *and* relay for remote builds. `cliant <relay-url> build .` submits a build, and `cliant serve -listen 0.0.0.0:17777` runs the relay that `sarver.exe` connects to.
- `campiler/`: packaging helpers for producing a Windows-targeted Go bootstrap/toolchain tree from the previously built host Go toolchain.
- `cleng/`: a Go-fronted Clang driver. Replaces Clang's C++ `main` with a Go entry point that links the LLVM/Clang static libs (built from the `llvm-project` submodule) via cgo, then forwards to `clang_main`. Cross-compiled to `windows/amd64` and packaged at `build/package/Xbax/cleng/bin/cleng.exe`.
- `llvm-project/`: git submodule pinned at `llvmorg-22.1.3`. Source for the Windows-targeted Clang/LLVM static libs that `cleng` links against.

## Requirements

### macOS (Homebrew)

A `Brewfile` at the repo root captures every native dependency — CMake +
Ninja, the host clang/clang++ used to bootstrap the LLVM cross-build, the
mingw-w64 cross toolchain used to target Windows, an existing `go` for
`GOROOT_BOOTSTRAP`, and Python 3.12 for the streaming client.

```bash
brew bundle
```

### Linux (Debian / Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build pkg-config git \
  clang lld \
  mingw-w64 \
  golang-go \
  python3 python3-pip
```

### Linux (Fedora)

```bash
sudo dnf install -y \
  cmake ninja-build pkgconf-pkg-config git \
  clang lld \
  mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads-static \
  golang \
  python3 python3-pip
```

Python packages (all platforms):

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

To additionally build the Go-fronted Clang driver:

```bash
git submodule update --init --recursive    # one-time, fetches llvm-project
cmake --build build --target cleng -j4
```

This first builds the Windows-targeted Clang/LLVM static libs from the
`llvm-project` submodule (under `build/engine/clang-windows-amd64/`) using
your host `clang`/`clang++` as the bootstrap — exactly the same role that
an existing `go` plays as `GOROOT_BOOTSTRAP` for the vendored Go tree —
then links `build/package/Xbax/cleng/bin/cleng.exe` via the mingw-w64
cross toolchain.

## Notes

- `campiler` is built from the vendored Go source, using the previously built host Go toolchain as `GOROOT_BOOTSTRAP`.
- `dastrabution.exe` is built with the custom Go toolchain and targets `windows/amd64`.
- `sarver` exposes an HTTP API for remote `go build` requests and automatically prefers the packaged `campiler/bin/go.exe` when it is available next to the Xbax package.
- `cliant` archives a Go workspace, uploads it to `sarver`, and saves the compiled binary locally.
- The local build also produces a native host copy of `cliant` under `build/host/cliant/`.
- Build artifacts are intentionally ignored by the repository `.gitignore`.

## Install And Manual HelloBax Test Flow

Use this flow when you want to install the packaged toolchain onto the Xbox from the UI, then manually prove that `cliant` can send a Go project to `sarver`, have it compiled remotely, and receive the built artifact back on your host machine.

1. Start the desktop client from the repo root:

```bash
python3 main.py
```

2. Connect to the devkit stream, then connect to the shell using the Visual Studio PIN shown in the RTSP view.
3. After the shell is connected, click `Install`.
   This builds the local package, syncs it to `D:/DevelopmentFiles/Xbax`, skips files that are already current, and updates remote files when the local packaged copy is newer.
4. On the host machine, start the `cliant` relay. This is the new entry point — `cliant` now both submits build jobs and serves them, while `sarver.exe` on the Xbox connects out to it to poll for work. Run it from the repo root:

```bash
./build/host/cliant/cliant serve -listen 0.0.0.0:17777
```

Example session output:

```text
cybertwip@cybertwips-MacBook-Pro xbax-stream % ./build/host/cliant/cliant serve -listen 0.0.0.0:17777
cliant relay listening on http://0.0.0.0:17777
cliant relay waiting for reverse workers from remote sarver instances
```

Optional flags:

- `-listen <addr>` — listen address, defaults to `0.0.0.0:17777`.
- `-max-upload-mib <N>` — maximum accepted source archive size, defaults to `256`.

5. In the remote Xbox shell, start `sarver.exe`. Reverse-poll mode is now the default — `sarver.exe` will dial back into the host relay (defaulting to `http://127.0.0.1:17777`) and poll for build jobs:

```text
d:
cd \DevelopmentFiles\Xbax\sarver
sarver.exe -reverse http://<host-ip>:17777
```

If the host relay is reachable at `http://127.0.0.1:17777` from the Xbox (e.g. through a tunnel), running `sarver.exe` with no flags is enough. To force the legacy push-mode HTTP server instead, pass `-listen 0.0.0.0:17777` explicitly. On startup the log prints `sarver reverse mode enabled; relay=...` and `sarver will pull build jobs from the host relay`.

6. On the host machine, open the sample project and submit a build to your own relay (`http://127.0.0.1:17777` from the same machine, or `http://<host-ip>:17777` from another host on the LAN):

```bash
cd HelloBax
```

Example session — building HelloBax for macOS Intel via the relay:

```text
cybertwip@cybertwips-MacBook-Pro HelloBax % ../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos darwin -goarch amd64 -o myapp
```

The same `cliant <relay-url> build ...` command shape works for every target. Pick the matching `-goos`/`-goarch`:

For a Windows build:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos windows -goarch amd64 -o myapp.exe
```

For macOS Intel:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos darwin -goarch amd64 -o myapp
```

For macOS Apple Silicon:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos darwin -goarch arm64 -o myapp
```

For Linux x86_64:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos linux -goarch amd64 -o myapp
```

For Linux arm64:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 build . -goos linux -goarch arm64 -o myapp
```

The relay queues the job, `sarver.exe` pulls it on its next poll, downloads the source archive, runs `go build` on the Xbox, posts the artifact back, and the host `cliant` writes it to `-o`.

You can health-check the relay at any time:

```bash
../build/host/cliant/cliant http://127.0.0.1:17777 health
```

`HelloBax` is the smallest manual test target in this repo and prints `Hello Bax`.

## Licensing

Project-specific files in this repository are covered by the Microsoft Public License in [LICENSE.MD](./LICENSE.MD).

Third-party code keeps its upstream license. In particular, the vendored Go source tree under `go/` retains its own license files, including `go/LICENSE`.
