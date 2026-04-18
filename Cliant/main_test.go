package main

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestParseBuildOptionsPassesThroughCompilerArgsForCppSource(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "hello.cpp")
	mustWriteTestFile(t, source, "int main() { return 0; }\n")

	opts, err := parseBuildOptions("http://127.0.0.1:17777", []string{
		source,
		"-stdlib=libc++",
		"-shared",
	})
	if err != nil {
		t.Fatalf("parseBuildOptions returned error: %v", err)
	}

	if opts.language != "cpp" {
		t.Fatalf("language = %q, want %q", opts.language, "cpp")
	}

	wantArgs := []string{"-stdlib=libc++", "-shared"}
	if !reflect.DeepEqual(opts.compilerArgs, wantArgs) {
		t.Fatalf("compilerArgs = %#v, want %#v", opts.compilerArgs, wantArgs)
	}
}

func TestParseBuildOptionsSupportsDoubleDashCompilerArgs(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "hello.cpp")
	mustWriteTestFile(t, source, "int main() { return 0; }\n")

	opts, err := parseBuildOptions("http://127.0.0.1:17777", []string{
		source,
		"--",
		"-I",
		"include",
		"-L",
		"lib",
		"-static",
	})
	if err != nil {
		t.Fatalf("parseBuildOptions returned error: %v", err)
	}

	wantArgs := []string{"-I", "include", "-L", "lib", "-static"}
	if !reflect.DeepEqual(opts.compilerArgs, wantArgs) {
		t.Fatalf("compilerArgs = %#v, want %#v", opts.compilerArgs, wantArgs)
	}
}

func TestParseBuildOptionsRejectsCompilerArgsForGoBuilds(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "main.go")
	mustWriteTestFile(t, source, "package main\nfunc main() {}\n")

	_, err := parseBuildOptions("http://127.0.0.1:17777", []string{
		source,
		"-shared",
	})
	if err == nil {
		t.Fatal("expected parseBuildOptions to reject compiler flags for a Go build")
	}
	if !strings.Contains(err.Error(), "compiler flags were provided") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func mustWriteTestFile(t *testing.T, path, contents string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}
