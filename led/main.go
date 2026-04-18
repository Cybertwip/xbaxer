package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const clengTargetTripleEnv = "CLENG_TARGET_TRIPLE"

func main() {
	tool, flavorArg, err := findBundledLinker(os.Args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "led: %v\n", err)
		os.Exit(1)
	}

	cmdArgs := append(flavorArg, os.Args[1:]...)
	cmd := exec.Command(tool, cmdArgs...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = os.Environ()

	if err := cmd.Run(); err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			os.Exit(exitErr.ExitCode())
		}
		fmt.Fprintf(os.Stderr, "led: %v\n", err)
		os.Exit(1)
	}
}

func findBundledLinker(argv []string) (string, []string, error) {
	exe, err := os.Executable()
	if err != nil {
		return "", nil, err
	}
	exeDir := filepath.Dir(exe)
	flavor := detectLinkerFlavor(exe, argv)

	for _, name := range linkerCandidates(flavor) {
		candidate := filepath.Join(exeDir, name)
		info, err := os.Stat(candidate)
		if err == nil && !info.IsDir() {
			if strings.EqualFold(filepath.Base(candidate), "lld.exe") || strings.EqualFold(filepath.Base(candidate), "lld") {
				return candidate, []string{"-flavor", flavor}, nil
			}
			return candidate, nil, nil
		}
	}
	return "", nil, fmt.Errorf("bundled linker not found next to %s", exe)
}

func detectLinkerFlavor(exe string, argv []string) string {
	base := strings.ToLower(filepath.Base(exe))
	switch base {
	case "ld64.lld.exe", "ld64.lld":
		return "darwin"
	case "ld.lld.exe", "ld.lld":
		return "gnu"
	}

	if isAppleTriple(os.Getenv(clengTargetTripleEnv)) || looksLikeDarwinLinkerArgs(argv) {
		return "darwin"
	}
	return "gnu"
}

func linkerCandidates(flavor string) []string {
	if flavor == "darwin" {
		return []string{"lld.exe", "lld", "ld64.lld.exe", "ld64.lld", "ld.lld.exe", "ld.lld"}
	}
	return []string{"lld.exe", "lld", "ld.lld.exe", "ld.lld", "ld64.lld.exe", "ld64.lld"}
}

func isAppleTriple(triple string) bool {
	return strings.Contains(triple, "-apple-")
}

func looksLikeDarwinLinkerArgs(argv []string) bool {
	for _, arg := range argv {
		switch {
		case arg == "-arch",
			arg == "-platform_version",
			arg == "-syslibroot",
			arg == "-headerpad",
			arg == "-headerpad_max_install_names",
			arg == "-framework",
			arg == "-lSystem":
			return true
		case strings.HasPrefix(arg, "-platform_version"),
			strings.HasPrefix(arg, "-macos_version_min"),
			strings.HasPrefix(arg, "-iphoneos_version_min"),
			strings.HasPrefix(arg, "-ios_simulator_version_min"):
			return true
		}
	}
	return false
}
