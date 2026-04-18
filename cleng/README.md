# cleng

`cleng` is a Go-fronted Clang driver. It replaces Clang's C++ `main` (the
`clang` / `clang-driver` entry point) with a Go program that links against the
LLVM/Clang libraries (built from the `llvm-project` submodule) via cgo. The
resulting executable is a regular Clang driver — same flags, same behaviour —
but the process entry point and any pre/post compilation orchestration runs in
Go.

## Build target

`cleng` is cross-compiled from macOS to **Windows / amd64** using the custom
Go toolchain shipped in this repository, and the produced PE binary is placed
at:

```
build/package/Xbax/cleng/bin/cleng.exe
```

## Layout

* `main.go` — Go entry point. Forwards `os.Args` to the Clang driver via cgo.
* `internal/cleng/` — Go-side compiler services (driver invocation,
  diagnostics handling, in-process orchestration helpers).
* `internal/cgobridge/` — `extern "C"` shim that exposes the C++ Clang
  driver entry point (`clang_main`) to Go. Also contains the build-tags and
  cgo `LDFLAGS` wiring for the prebuilt Clang static libraries.

## libclang / Clang driver

We do **not** rebuild Clang from this Go module. Instead, Clang is built
once (out-of-band, from the `llvm-project` submodule) for the
`x86_64-w64-mingw32` target with all driver / frontend / codegen libraries
as static archives, and dropped into:

```
build/engine/clang-windows-amd64/
  ├── include/
  └── lib/        # libclangDriver.a, libclangFrontend.a, libLLVM*.a, ...
```

The cgo bridge in `internal/cgobridge` links against those archives. To
re-point the build at a different Clang prefix, set the `CLENG_CLANG_PREFIX`
environment variable when invoking `go build`.
