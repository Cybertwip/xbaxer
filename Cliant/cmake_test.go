package main

import (
	"path/filepath"
	"reflect"
	"testing"
)

func TestParseNinjaCommandLineUnwrapsLinkShellGlue(t *testing.T) {
	t.Parallel()

	workingDirectory, argv, err := parseNinjaCommandLine(
		"/tmp/build",
		`: && /usr/bin/clang++ --target=x86_64-w64-mingw32 -shared -o bin/d3d11_x.dll foo.obj && :`,
	)
	if err != nil {
		t.Fatalf("parseNinjaCommandLine returned error: %v", err)
	}

	if workingDirectory != "/tmp/build" {
		t.Fatalf("workingDirectory = %q, want %q", workingDirectory, "/tmp/build")
	}

	want := []string{
		"/usr/bin/clang++",
		"--target=x86_64-w64-mingw32",
		"-shared",
		"-o",
		"bin/d3d11_x.dll",
		"foo.obj",
	}
	if !reflect.DeepEqual(argv, want) {
		t.Fatalf("argv = %#v, want %#v", argv, want)
	}
}

func TestRewriteBuildStepForRemoteRelativisesAbsoluteProjectPaths(t *testing.T) {
	t.Parallel()

	sourceRoot := filepath.Clean("/tmp/project")
	workingDirectory := filepath.Join(sourceRoot, ".cliant-cmake", "TriangleWinDurango")
	step, artifactPath, err := rewriteBuildStepForRemote(sourceRoot, workingDirectory, []string{
		"/usr/bin/clang++",
		"--target=x86_64-w64-mingw32",
		"-I" + filepath.Join(sourceRoot, "windurango"),
		"-o",
		filepath.Join(workingDirectory, "bin", "TriangleWinDurango.exe"),
		"-c",
		filepath.Join(sourceRoot, "TriangleApp.cpp"),
	})
	if err != nil {
		t.Fatalf("rewriteBuildStepForRemote returned error: %v", err)
	}

	if artifactPath != ".cliant-cmake/TriangleWinDurango/bin/TriangleWinDurango.exe" {
		t.Fatalf("artifactPath = %q", artifactPath)
	}

	wantArgs := []string{
		"clang++",
		"--target=x86_64-w64-mingw32",
		"-I../../windurango",
		"-o",
		"bin/TriangleWinDurango.exe",
		"-c",
		"../../TriangleApp.cpp",
	}
	if !reflect.DeepEqual(step.Args, wantArgs) {
		t.Fatalf("step.Args = %#v, want %#v", step.Args, wantArgs)
	}

	if step.WorkingDirectory != ".cliant-cmake/TriangleWinDurango" {
		t.Fatalf("step.WorkingDirectory = %q", step.WorkingDirectory)
	}
}
