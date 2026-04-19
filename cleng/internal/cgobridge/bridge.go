//go:build windows && amd64

// Package cgobridge exposes the in-process Clang driver to Go.
//
// The actual C++ shim that calls into Clang's driver (clang::driver::Driver
// or, simpler, the upstream clang_main / cc1_main entry points re-exported
// with C linkage) lives in bridge_windows_amd64.cpp. This file declares the cgo binding
// and translates Go []string into a C argv.
package cgobridge

/*
#cgo CXXFLAGS: -std=c++17
#include <stdlib.h>

extern int cleng_clang_main(int argc, char **argv);
*/
import "C"

import (
	"unsafe"
)

// ClangMain forwards argv to the linked-in Clang driver and returns its
// exit code.
func ClangMain(argv []string) (int, error) {
	cArgs := make([]*C.char, len(argv)+1)
	for i, s := range argv {
		cArgs[i] = C.CString(s)
	}
	cArgs[len(argv)] = nil
	defer func() {
		for i := 0; i < len(argv); i++ {
			C.free(unsafe.Pointer(cArgs[i]))
		}
	}()

	rc := C.cleng_clang_main(C.int(len(argv)), (**C.char)(unsafe.Pointer(&cArgs[0])))
	return int(rc), nil
}
