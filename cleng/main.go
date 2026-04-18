// cleng is a Go-fronted Clang driver. The process entry point lives in Go,
// but the actual compilation work is performed by the Clang/LLVM libraries
// linked in via cgo (see internal/cgobridge). This lets us keep the exact
// PE layout, calling convention and driver semantics of upstream clang.exe
// while letting the surrounding orchestration logic live in Go alongside the
// rest of the xbax tooling (campiler, gatter, sarver, cliant, ...).
package main

import (
	"fmt"
	"os"

	"cleng/internal/cleng"
)

func main() {
	code, err := cleng.Run(os.Args)
	if err != nil {
		fmt.Fprintf(os.Stderr, "cleng: %v\n", err)
		if code == 0 {
			code = 1
		}
	}
	os.Exit(code)
}
