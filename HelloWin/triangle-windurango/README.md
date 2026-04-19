# Triangle WinDurango Proof Of Concept

This sample strips `HelloWin/okane` down to the animated triangle path and links
against WinDurango-shaped DLL names instead of the usual GameCore libraries.

- `TriangleWinDurango` is a plain Windows windowed app.
- `d3d11_x.dll` forwards the D3D11 entrypoints the sample needs onto desktop
  `d3d11.dll`, so the executable imports `d3d11_x` instead of `d3d11`.
- `kernelx.dll` provides the small WinDurango-style exports the sample uses to
  stamp the window title.

Local configure + build:

```sh
export XBAX_WINDOWS_SYSROOT=/path/to/your/windows-cross-sysroot

cmake -S HelloWin/triangle-windurango \
  -B HelloWin/triangle-windurango/build-local \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=HelloWin/triangle-windurango/cmake/xbax-remote-windows-toolchain.cmake

cmake --build HelloWin/triangle-windurango/build-local --target TriangleWinDurango
```

`XBAX_WINDOWS_SYSROOT` should point at a MinGW or Windows SDK style sysroot that
contains the Windows headers and import libraries for `x86_64-w64-mingw32`.
Without that sysroot, local configure still works, but the actual compile will
stop at `windows.h` lookup.

That produces:

- `TriangleWinDurango.exe`
- `d3d11_x.dll`
- `kernelx.dll`

Remote compile replay through `cliant` + `sarver`:

```sh
cliant http://<sarver>:17777 cmake-build HelloWin/triangle-windurango \
  -target TriangleWinDurango \
  -o triangle-windurango.exe
```

The default toolchain file is `cmake/xbax-remote-windows-toolchain.cmake`, and
the generated Ninja graph is stored under `.cliant-cmake/`.

Packaging and deploy from the launcher UI:

- Build `TriangleWinDurango` first.
- In the launcher, use `Package` to choose the build output folder and an output folder for the generated `.appx`.
- Use `Deploy` to choose the same build output folder and push the packaged app to the console through Device Portal.

The launcher expects the vendored packer at `third_party/appx-util/appx-util` and
builds it locally into `third_party/appx-util/build` on demand.
