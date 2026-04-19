# Okaneizer Reference

`Reference/okane` is a standalone C++/HLSL sample with local menu and HUD logic, targeting the same GDK desktop and Xbox One/Series presets as the other reference projects.

What this sample includes:

- a D3D12/HLSL app scaffold based on the repo's GDK reference
- a local UI/menu state model that does not depend on an embedded scripting/runtime bridge
- local toolchain files and presets for GDK desktop, Xbox One, and Scarlett

Primary build targets:

- `go-toolchain`: builds the vendored Go toolchain
- `okane-go-version`: prints the built Go version
- `okane-go-bundle`: copies the built GOROOT into `build/bundle/go`
- `okane-go-build`: runs `go build` for the configured Go target
- `okane-go-test`: runs `go test`
- `OkaneizerReference`: builds the C++/HLSL app

Typical flow:

```powershell
cmake --preset windows-desktop
cmake --build build --target OkaneizerReference --config Debug
```

Runtime notes:

- the shared UI/state structs still live in `include/OkaneBridge.h`
- the app currently runs fully locally and does not build or link the Go bridge
- the earlier Go sources remain in the repo for reference and future rework, but are not part of the renderer path

The earlier `hello.go` file is still here as a tiny manual Go smoke test and is not part of the renderer bridge.
