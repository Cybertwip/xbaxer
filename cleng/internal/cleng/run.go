// Package cleng exposes the Go-side compiler services that wrap Clang.
//
// Run is the single entry point used by main.go. It performs any Go-side
// pre-processing of the command line (logging, env setup, intercepting
// xbax-specific flags) and then dispatches to the cgo bridge that calls into
// the linked-in Clang driver.
package cleng

import (
	"os"
	"path/filepath"
	"sort"
	"strings"

	"cleng/internal/cgobridge"
)

// Run invokes the Clang driver with the given argv (argv[0] is the program
// name, as expected by Clang). It returns the driver exit code.
//
// All compilation work — preprocessing, parsing, sema, codegen, linking —
// happens inside the in-process Clang libraries. We do not fork an external
// clang binary.
func Run(argv []string) (int, error) {
	argv = injectResourceDir(argv)
	argv = injectSysrootIncludes(argv)
	return cgobridge.ClangMain(argv)
}

// injectResourceDir prepends `-resource-dir <dir>` to argv if (a) the user
// hasn't already passed one and (b) we can locate clang's compiler-builtin
// header tree relative to the cleng binary.
//
// Layout we look for (matches what build-clang-engine.sh installs):
//
//	<exe-dir>/lib/clang/<ver>/include/stddef.h
//	<exe-dir>/../lib/clang/<ver>/include/stddef.h
//
// The compiler-builtin headers (stddef.h, stdarg.h, stdint.h, intrinsics)
// are owned by the compiler itself, not the platform SDK, so they need to
// travel with cleng.exe. Without an explicit `-resource-dir` clang searches
// relative to its own binary's *install* location (where it was built),
// which is wrong as soon as we deploy cleng to another machine.
func injectResourceDir(argv []string) []string {
	for _, arg := range argv {
		if arg == "-resource-dir" || strings.HasPrefix(arg, "-resource-dir=") {
			return argv
		}
	}
	dir := findResourceDir()
	if dir == "" {
		return argv
	}
	return insertAfterArgv0(argv, "-resource-dir", dir)
}

// injectSysrootIncludes appends `-isystem <path>` entries pointing at the
// bundled mingw-w64 sysroot (libc + libstdc++ headers) when the build's
// effective target triple is x86_64-w64-mingw32. These are *standard*
// C/C++ headers — stdio.h, vector, sstream — that the compiler itself
// does not own. They are bundled at build time by CMake (see
// cleng/scripts/copy_sysroot.cmake) at <exe-dir>/../sysroot/<triple>/.
//
// We only inject them when the user is actually targeting mingw; cross-
// builds for darwin/linux/etc. need their own sysroot supplied by the
// developer (via `--sysroot=` or explicit `-isystem` cgo flags), as the
// xbax toolchain only commits to bundling the windows target's stdlib.
//
// Adding `-isystem` (rather than replacing the search path with
// `-nostdinc`) is intentional: the bundled sysroot then layers on top of
// whatever clang would have searched, which keeps user-supplied
// `-I`/`-isystem` flags first (correct precedence) while still resolving
// stdio.h/vector when nothing else does.
func injectSysrootIncludes(argv []string) []string {
	triple := effectiveTargetTriple(argv)
	if !isMingwTriple(triple) {
		return argv
	}
	root := findSysrootDir(triple)
	if root == "" {
		return argv
	}
	include := filepath.Join(root, "include")
	if _, err := os.Stat(filepath.Join(include, "stdio.h")); err != nil {
		return argv
	}

	// Order matters: C++ headers first (so <cstdio> wraps <stdio.h>
	// correctly), then the target-specific bits/, then the plain C
	// include dir. We pick the highest GCC version present.
	var includes []string
	if cxxVerDir := findHighestCxxVersionDir(filepath.Join(include, "c++")); cxxVerDir != "" {
		includes = append(includes, cxxVerDir)
		// libstdc++ pulls bits/ from a target subdir.
		targetSub := filepath.Join(cxxVerDir, triple)
		if _, err := os.Stat(targetSub); err == nil {
			includes = append(includes, targetSub)
		}
	}
	includes = append(includes, include)

	extra := make([]string, 0, len(includes)*2)
	for _, dir := range includes {
		extra = append(extra, "-isystem", dir)
	}
	return insertAfterArgv0(argv, extra...)
}

func insertAfterArgv0(argv []string, extra ...string) []string {
	if len(extra) == 0 {
		return argv
	}
	if len(argv) == 0 {
		return append([]string{"cleng"}, extra...)
	}
	out := make([]string, 0, len(argv)+len(extra))
	out = append(out, argv[0])
	out = append(out, extra...)
	out = append(out, argv[1:]...)
	return out
}

// effectiveTargetTriple finds the value of --target=/-target in argv, or
// returns the empty string if none was supplied. We look at the *last*
// occurrence so a later override wins, matching clang's own behaviour.
func effectiveTargetTriple(argv []string) string {
	triple := ""
	for i := 0; i < len(argv); i++ {
		arg := argv[i]
		switch {
		case strings.HasPrefix(arg, "--target="):
			triple = strings.TrimPrefix(arg, "--target=")
		case strings.HasPrefix(arg, "-target="):
			triple = strings.TrimPrefix(arg, "-target=")
		case arg == "-target" && i+1 < len(argv):
			triple = argv[i+1]
			i++
		}
	}
	return triple
}

func isMingwTriple(triple string) bool {
	return strings.HasSuffix(triple, "-w64-mingw32") || strings.HasSuffix(triple, "-w64-windows-gnu")
}

func findResourceDir() string {
	for _, root := range exeRelativeRoots() {
		base := filepath.Join(root, "lib", "clang")
		entries, err := os.ReadDir(base)
		if err != nil {
			continue
		}
		for _, entry := range entries {
			if !entry.IsDir() {
				continue
			}
			candidate := filepath.Join(base, entry.Name())
			if _, err := os.Stat(filepath.Join(candidate, "include", "stddef.h")); err == nil {
				return candidate
			}
		}
	}
	return ""
}

// findSysrootDir locates <exe-dir>[/..]/sysroot/<triple>/ — the directory
// whose include/ subtree contains stdio.h.
func findSysrootDir(triple string) string {
	for _, root := range exeRelativeRoots() {
		candidate := filepath.Join(root, "sysroot", triple)
		if _, err := os.Stat(filepath.Join(candidate, "include", "stdio.h")); err == nil {
			return candidate
		}
	}
	return ""
}

// findHighestCxxVersionDir scans a directory like include/c++/ for
// numeric-named version subdirectories and returns the highest one (lexical
// sort works for libstdc++'s "<major>.<minor>.<patch>" naming so long as
// majors stay below 10; otherwise version-aware comparison kicks in via
// length-then-lex).
func findHighestCxxVersionDir(parent string) string {
	entries, err := os.ReadDir(parent)
	if err != nil {
		return ""
	}
	var dirs []string
	for _, entry := range entries {
		if entry.IsDir() {
			dirs = append(dirs, entry.Name())
		}
	}
	if len(dirs) == 0 {
		return ""
	}
	sort.Slice(dirs, func(i, j int) bool {
		// longer string sorts later (so "15.2.0" > "9.4.0"), then
		// lexical for ties. Good enough for libstdc++ versions.
		if len(dirs[i]) != len(dirs[j]) {
			return len(dirs[i]) < len(dirs[j])
		}
		return dirs[i] < dirs[j]
	})
	return filepath.Join(parent, dirs[len(dirs)-1])
}

// exeRelativeRoots returns the candidate directories we search for bundled
// resources (resource dir, sysroot). The standard layout is
// <prefix>/bin/cleng.exe + <prefix>/lib + <prefix>/sysroot, so <exe-dir>/..
// is the primary root; <exe-dir> itself covers a flat unzip.
func exeRelativeRoots() []string {
	exe, err := os.Executable()
	if err != nil {
		return nil
	}
	exeDir := filepath.Dir(exe)
	return []string{filepath.Dir(exeDir), exeDir}
}
