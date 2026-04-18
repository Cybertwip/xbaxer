// bridge_link.go centralises the cgo linker flags for the prebuilt
// LLVM/Clang static libraries. Keeping them in their own file (rather than
// in bridge.go) makes it easy for the CMake build to regenerate or override
// this file when the Clang prefix moves.
//
// The default flags assume the layout produced by the `clang-windows-amd64`
// CMake target:
//
//	${CMAKE_BINARY_DIR}/engine/clang-windows-amd64/
//	    include/
//	    lib/libclang*.a, libLLVM*.a
//
// Override at build time with:
//
//	CGO_LDFLAGS="-L/path/to/clang/lib -lclangDriver ..." \
//	CGO_CXXFLAGS="-I/path/to/clang/include" \
//	go build ./...

package cgobridge

/*
#cgo windows,amd64 LDFLAGS: -L${SRCDIR}/../../../build/engine/clang-windows-amd64/lib
#cgo windows,amd64 LDFLAGS: -lclangDriver -lclangFrontend -lclangFrontendTool
#cgo windows,amd64 LDFLAGS: -lclangCodeGen -lclangSerialization -lclangSema
#cgo windows,amd64 LDFLAGS: -lclangAnalysis -lclangEdit -lclangAST -lclangASTMatchers
#cgo windows,amd64 LDFLAGS: -lclangParse -lclangLex -lclangBasic
#cgo windows,amd64 LDFLAGS: -lLLVMOption -lLLVMSupport -lLLVMDemangle
#cgo windows,amd64 LDFLAGS: -lstdc++ -lpthread
*/
import "C"
