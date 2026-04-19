# Triangle WinDurango Proof Of Concept

This sample strips `HelloWin/okane` down to the animated triangle path and links
against WinDurango-shaped DLL names instead of the usual GameCore libraries.

- `TriangleWinDurango` is a plain Windows windowed app.
- `d3d11_x.dll` forwards the D3D11 entrypoints the sample needs onto desktop
  `d3d11.dll`, so the executable imports `d3d11_x` instead of `d3d11`.
- `kernelx.dll` provides the small WinDurango-style exports the sample uses to
  stamp the window title.

Local CMake configure + remote replay:

```sh
cliant http://<sarver>:17777 cmake-build HelloWin/triangle-windurango \
  -target TriangleWinDurango \
  -o triangle-windurango.exe
```

The default toolchain file is `cmake/xbax-remote-windows-toolchain.cmake`, and
the generated Ninja graph is stored under `.cliant-cmake/`.
