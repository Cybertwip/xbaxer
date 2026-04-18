// bridge_link.go — hand-curated cgo flags for the cleng bridge.
//
// The full list of -L/-l flags for the prebuilt LLVM/Clang archives lives
// in bridge_link_generated.go, which CMake regenerates after the
// clang-engine target finishes (see cleng/scripts/gen_bridge_link.cmake).
// Keep this file limited to flags that don't depend on the on-disk lib
// inventory: include paths, language standard, and system libraries the
// driver pulls in via WIN32 calls.
//
// LLVM is built with -fno-rtti -fno-exceptions, so anything we compile
// against its headers (driver.cpp, cc1*_main.cpp, bridge.cpp) must match
// or the vtable layouts won't line up at link time.

package cgobridge

/*
#cgo windows,amd64 CXXFLAGS: -std=c++17 -fno-rtti -fno-exceptions -DNDEBUG
#cgo windows,amd64 CXXFLAGS: -Wno-deprecated-declarations -Wno-unused-parameter
#cgo windows,amd64 CXXFLAGS: -Wno-attributes -Wno-class-memaccess
#cgo windows,amd64 CXXFLAGS: -Wno-comment -Wno-unused-function

// Win32 system libraries that Clang's driver, libSupport, and libDebugInfo
// link against (registry, COM, version info, sockets, debug help, etc.).
#cgo windows,amd64 LDFLAGS: -lversion -luuid -lole32 -loleaut32
#cgo windows,amd64 LDFLAGS: -lws2_32 -lntdll -ladvapi32 -lpsapi
#cgo windows,amd64 LDFLAGS: -lshell32 -limagehlp -lshlwapi -lmsvcrt
#cgo windows,amd64 LDFLAGS: -static -lstdc++ -lpthread
*/
import "C"
