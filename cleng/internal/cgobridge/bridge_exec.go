//go:build !(windows && amd64)

package cgobridge

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const (
	hostClangEnv   = "XBAX_HOST_CLANG"
	hostClangXXEnv = "XBAX_HOST_CLANGXX"
)

// ClangMain falls back to an external host clang driver when cleng is built
// for non-windows hosts. This keeps local CMake graph generation usable
// without needing to link the in-process LLVM engine into the host binary.
func ClangMain(argv []string) (int, error) {
	if len(argv) == 0 {
		return 1, fmt.Errorf("missing argv[0]")
	}

	tool := hostCompilerBinary(argv[0], argv[1:])
	cmd := exec.Command(tool, argv[1:]...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = os.Environ()

	if err := cmd.Run(); err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			return exitErr.ExitCode(), nil
		}
		return 1, err
	}
	return 0, nil
}

func hostCompilerBinary(argv0 string, argv []string) string {
	if wantsCXXDriver(argv0, argv) {
		if override := os.Getenv(hostClangXXEnv); override != "" {
			return override
		}
		return "clang++"
	}
	if override := os.Getenv(hostClangEnv); override != "" {
		return override
	}
	return "clang"
}

func wantsCXXDriver(argv0 string, argv []string) bool {
	base := strings.ToLower(filepath.Base(argv0))
	switch base {
	case "cleng++", "clang++", "c++":
		return true
	}

	for i := 0; i < len(argv); i++ {
		arg := argv[i]
		switch {
		case arg == "-x" && i+1 < len(argv) && strings.EqualFold(argv[i+1], "c++"):
			return true
		case arg == "--driver-mode=g++" || arg == "--driver-mode=cpp":
			return true
		case strings.HasPrefix(arg, "-xc++"):
			return true
		}
	}
	return false
}
