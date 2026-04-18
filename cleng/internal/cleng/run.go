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
	argv = prepareArgv(argv)
	return cgobridge.ClangMain(argv)
}

func prepareArgv(argv []string) []string {
	// When clang re-invokes itself for a direct integrated-tool entry point
	// (`-cc1`, `-cc1as`, `-cc1gen-reproducer`), that flag must remain argv[1].
	// Prepending our own resource-dir/sysroot flags would shift it out of
	// position and make the subprocess parse low-level cc1 flags as if they
	// were top-level driver arguments.
	if isDirectIntegratedToolInvocation(argv) {
		return argv
	}
	argv = injectResourceDir(argv)
	argv = injectBundledTargetRuntime(argv)
	return argv
}

func isDirectIntegratedToolInvocation(argv []string) bool {
	return len(argv) >= 2 && strings.HasPrefix(argv[1], "-cc1")
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

var bundleRootsFunc = exeRelativeRoots

// injectBundledTargetRuntime wires in the packaged target-facing SDK/sysroot
// data when the caller selected a known target triple and did not already
// provide an explicit stdlib/sysroot override of their own.
//
// The build packages these next to cleng under <prefix>/sysroot/<triple>/:
//   - mingw-w64 headers/import libs/libgcc runtime pieces for windows
//   - the active Apple SDK for darwin (when captured on an Apple host)
//   - glibc/libstdc++/libgcc payloads for linux triplets (when installed)
//
// Darwin is the simplest case: `-isysroot` against the bundled SDK is enough
// for Clang to recover libc++, libc and framework search paths. GNU-ish
// targets still benefit from some manual hints, so alongside `--sysroot=` we
// add `-isystem` / `-L` / `-B` pointing at the bundled C++ and libgcc trees.
//
// Explicit user flags always win. If the argv already contains its own
// `--sysroot=` / `-isysroot` / `-syslibroot` or an opt-out like `-nostdlib`,
// we leave the command line untouched.
func injectBundledTargetRuntime(argv []string) []string {
	triple := effectiveTargetTriple(argv)
	if triple == "" || hasExplicitRuntimeOverride(argv) {
		return argv
	}

	if isAppleTriple(triple) {
		if sdk := findBundledAppleSDK(triple); sdk != "" {
			return insertAfterArgv0(argv, "-isysroot", sdk)
		}
		return argv
	}

	if extra := findBundledGNUFlags(triple); len(extra) != 0 {
		return insertAfterArgv0(argv, extra...)
	}

	// Backward compatibility with packages built before we started bundling
	// per-target runtime libraries. Those only shipped the mingw include tree.
	if !isMingwTriple(triple) {
		return argv
	}
	return injectLegacyMingwIncludes(argv, triple)
}

func hasExplicitRuntimeOverride(argv []string) bool {
	for i := 0; i < len(argv); i++ {
		arg := argv[i]
		switch {
		case arg == "-isysroot",
			arg == "-syslibroot",
			arg == "--sysroot",
			arg == "-nostdinc",
			arg == "-nostdinc++",
			arg == "-nostdlib",
			arg == "-nostdlib++",
			arg == "-nodefaultlibs":
			return true
		case strings.HasPrefix(arg, "--sysroot="),
			strings.HasPrefix(arg, "-syslibroot="),
			strings.HasPrefix(arg, "-isysroot="):
			return true
		}
	}
	return false
}

func findBundledAppleSDK(triple string) string {
	root := findTargetBundleDir(triple)
	if root == "" {
		return ""
	}
	if _, err := os.Stat(filepath.Join(root, "usr", "include", "stdio.h")); err != nil {
		return ""
	}
	return root
}

func findBundledGNUFlags(triple string) []string {
	root := findTargetBundleDir(triple)
	if root == "" {
		return nil
	}
	sysroot := findBundledGNUSysroot(root)
	if sysroot == "" {
		return nil
	}

	extra := []string{"--sysroot=" + sysroot}
	if gccLib := findHighestGCCLibDir(root, triple); gccLib != "" {
		extra = append(extra, "-B"+gccLib, "-L", gccLib)
	}
	for _, dir := range findBundledGNUIncludeDirs(root, sysroot, triple) {
		extra = append(extra, "-isystem", dir)
	}
	for _, dir := range findBundledGNULibraryDirs(root, sysroot, triple) {
		extra = append(extra, "-L", dir)
	}
	return extra
}

func findBundledGNUIncludeDirs(root, sysroot, triple string) []string {
	var includes []string
	for _, cxxParent := range []string{
		filepath.Join(root, "include", "c++"),
		filepath.Join(sysroot, "include", "c++"),
		filepath.Join(sysroot, "usr", "include", "c++"),
	} {
		if cxxVerDir := findHighestVersionDir(cxxParent); cxxVerDir != "" {
			includes = appendUniqueDir(includes, cxxVerDir)
			for _, alias := range tripleAliases(triple) {
				includes = appendUniqueDir(includes, filepath.Join(cxxVerDir, alias))
			}
			includes = appendUniqueDir(includes, filepath.Join(cxxVerDir, "backward"))
		}
	}
	for _, include := range []string{
		filepath.Join(root, "include"),
		filepath.Join(sysroot, "include"),
		filepath.Join(sysroot, "usr", "include"),
	} {
		if hasHeader(include, "stdio.h") {
			includes = appendUniqueDir(includes, include)
		}
	}
	return includes
}

func findBundledGNULibraryDirs(root, sysroot, triple string) []string {
	var libs []string
	for _, dir := range []string{
		filepath.Join(root, "lib"),
		filepath.Join(root, "lib64"),
		filepath.Join(sysroot, "lib"),
		filepath.Join(sysroot, "lib64"),
		filepath.Join(sysroot, "usr", "lib"),
		filepath.Join(sysroot, "usr", "lib64"),
	} {
		libs = appendUniqueDir(libs, dir)
	}
	for _, alias := range tripleAliases(triple) {
		libs = appendUniqueDir(libs, filepath.Join(sysroot, "lib", alias))
		libs = appendUniqueDir(libs, filepath.Join(sysroot, "usr", "lib", alias))
	}
	return libs
}

func findBundledGNUSysroot(root string) string {
	for _, candidate := range []string{root, filepath.Join(root, "sysroot")} {
		switch {
		case hasHeader(filepath.Join(candidate, "include"), "stdio.h"):
			return candidate
		case hasHeader(filepath.Join(candidate, "usr", "include"), "stdio.h"):
			return candidate
		}
	}
	return ""
}

func findHighestGCCLibDir(root, triple string) string {
	for _, alias := range tripleAliases(triple) {
		if dir := findHighestVersionDir(filepath.Join(root, "lib", "gcc", alias)); dir != "" {
			return dir
		}
	}
	return ""
}

func appendUniqueDir(list []string, dir string) []string {
	info, err := os.Stat(dir)
	if err != nil || !info.IsDir() {
		return list
	}
	for _, existing := range list {
		if existing == dir {
			return list
		}
	}
	return append(list, dir)
}

func hasHeader(parent, name string) bool {
	_, err := os.Stat(filepath.Join(parent, name))
	return err == nil
}

func tripleAliases(triple string) []string {
	aliases := []string{triple}
	switch {
	case strings.HasSuffix(triple, "-linux-gnu"):
		aliases = append(aliases, strings.Replace(triple, "-linux-gnu", "-unknown-linux-gnu", 1))
	case strings.HasSuffix(triple, "-w64-mingw32"):
		aliases = append(aliases, strings.Replace(triple, "-w64-mingw32", "-w64-windows-gnu", 1))
	}
	return aliases
}

func isAppleTriple(triple string) bool {
	return strings.Contains(triple, "-apple-")
}

func injectLegacyMingwIncludes(argv []string, triple string) []string {
	root := findLegacySysrootDir(triple)
	if root == "" {
		return argv
	}
	include := filepath.Join(root, "include")

	// Order matters: C++ headers first (so <cstdio> wraps <stdio.h>
	// correctly), then the target-specific bits/, then the plain C
	// include dir. We pick the highest GCC version present.
	var includes []string
	if cxxVerDir := findHighestVersionDir(filepath.Join(include, "c++")); cxxVerDir != "" {
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

func findTargetBundleDir(triple string) string {
	for _, root := range bundleRootsFunc() {
		candidate := filepath.Join(root, "sysroot", triple)
		info, err := os.Stat(candidate)
		if err == nil && info.IsDir() {
			return candidate
		}
	}
	return ""
}

// findLegacySysrootDir locates the older include-only mingw layout:
// <exe-dir>[/..]/sysroot/<triple>/include/stdio.h.
func findLegacySysrootDir(triple string) string {
	candidate := findTargetBundleDir(triple)
	if candidate == "" {
		return ""
	}
	if _, err := os.Stat(filepath.Join(candidate, "include", "stdio.h")); err != nil {
		return ""
	}
	return candidate
}

// findHighestVersionDir scans a directory like include/c++/ or lib/gcc/<triple>/
// and returns the highest numeric-named version subdirectory (lexical
// sort works for libstdc++'s "<major>.<minor>.<patch>" naming so long as
// majors stay below 10; otherwise version-aware comparison kicks in via
// length-then-lex).
func findHighestVersionDir(parent string) string {
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
