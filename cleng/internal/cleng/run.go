// Package cleng exposes the Go-side compiler services that wrap Clang.
//
// Run is the single entry point used by main.go. It performs any Go-side
// pre-processing of the command line (logging, env setup, intercepting
// xbax-specific flags) and then dispatches to the cgo bridge that calls into
// the linked-in Clang driver.
package cleng

import (
	"cleng/internal/cgobridge"
)

// Run invokes the Clang driver with the given argv (argv[0] is the program
// name, as expected by Clang). It returns the driver exit code.
//
// All compilation work — preprocessing, parsing, sema, codegen, linking —
// happens inside the in-process Clang libraries. We do not fork an external
// clang binary.
func Run(argv []string) (int, error) {
	return cgobridge.ClangMain(argv)
}
