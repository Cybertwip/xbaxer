package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestPreparePlanStepFilesystemCreatesOutputDirectories(t *testing.T) {
	t.Parallel()

	sourceDir := t.TempDir()
	workingDirectory := filepath.Join(sourceDir, ".cliant-cmake", "TriangleCpp")
	step := buildStep{
		Args: []string{
			"cleng++",
			"-MF", "CMakeFiles/TriangleCpp.dir/TriangleApp.cpp.obj.d",
			"-o", "CMakeFiles/TriangleCpp.dir/TriangleApp.cpp.obj",
			"-Wl,--out-implib,lib/libTriangleCpp.dll.a",
			"-o", "bin/TriangleCpp.exe",
		},
	}

	if err := preparePlanStepFilesystem(sourceDir, workingDirectory, step); err != nil {
		t.Fatalf("preparePlanStepFilesystem returned error: %v", err)
	}

	for _, dir := range []string{
		filepath.Join(workingDirectory, "CMakeFiles", "TriangleCpp.dir"),
		filepath.Join(workingDirectory, "lib"),
		filepath.Join(workingDirectory, "bin"),
	} {
		info, err := os.Stat(dir)
		if err != nil {
			t.Fatalf("stat %s: %v", dir, err)
		}
		if !info.IsDir() {
			t.Fatalf("%s is not a directory", dir)
		}
	}
}
