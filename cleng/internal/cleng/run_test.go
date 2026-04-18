package cleng

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestInjectBundledTargetRuntimeAppleSDK(t *testing.T) {
	root := t.TempDir()
	sdk := filepath.Join(root, "sysroot", "aarch64-apple-darwin")
	mustMkdirAll(t, filepath.Join(sdk, "usr", "include"))
	mustWriteFile(t, filepath.Join(sdk, "usr", "include", "stdio.h"))

	withBundleRoots(t, root)

	got := injectBundledTargetRuntime([]string{"cleng", "--target=aarch64-apple-darwin", "hello.c"})
	want := []string{"cleng", "-isysroot", sdk, "--target=aarch64-apple-darwin", "hello.c"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("argv mismatch:\n got: %#v\nwant: %#v", got, want)
	}
}

func TestInjectBundledTargetRuntimeLinuxGNU(t *testing.T) {
	root := t.TempDir()
	bundle := filepath.Join(root, "sysroot", "x86_64-linux-gnu")
	sysroot := filepath.Join(bundle, "sysroot")
	gccLib := filepath.Join(bundle, "lib", "gcc", "x86_64-unknown-linux-gnu", "15.2.0")
	cxx := filepath.Join(bundle, "include", "c++", "15.2.0")
	cxxTarget := filepath.Join(cxx, "x86_64-unknown-linux-gnu")
	sysInclude := filepath.Join(sysroot, "usr", "include")
	sysLib := filepath.Join(sysroot, "usr", "lib")

	for _, dir := range []string{gccLib, cxxTarget, sysInclude, sysLib} {
		mustMkdirAll(t, dir)
	}
	mustWriteFile(t, filepath.Join(sysInclude, "stdio.h"))

	withBundleRoots(t, root)

	got := injectBundledTargetRuntime([]string{"cleng", "--target=x86_64-linux-gnu", "hello.cc"})
	want := []string{
		"cleng",
		"--sysroot=" + sysroot,
		"-B" + gccLib,
		"-L", gccLib,
		"-isystem", cxx,
		"-isystem", cxxTarget,
		"-isystem", sysInclude,
		"-L", filepath.Join(bundle, "lib"),
		"-L", sysLib,
		"--target=x86_64-linux-gnu",
		"hello.cc",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("argv mismatch:\n got: %#v\nwant: %#v", got, want)
	}
}

func TestInjectBundledTargetRuntimeMingwBundle(t *testing.T) {
	root := t.TempDir()
	bundle := filepath.Join(root, "sysroot", "x86_64-w64-mingw32")
	gccLib := filepath.Join(bundle, "lib", "gcc", "x86_64-w64-mingw32", "15.2.0")
	cxx := filepath.Join(bundle, "include", "c++", "15.2.0")
	cxxTarget := filepath.Join(cxx, "x86_64-w64-mingw32")
	include := filepath.Join(bundle, "include")
	lib := filepath.Join(bundle, "lib")

	for _, dir := range []string{gccLib, cxxTarget, include, lib} {
		mustMkdirAll(t, dir)
	}
	mustWriteFile(t, filepath.Join(include, "stdio.h"))

	withBundleRoots(t, root)

	got := injectBundledTargetRuntime([]string{"cleng", "--target=x86_64-w64-mingw32", "hello.cc"})
	want := []string{
		"cleng",
		"--sysroot=" + bundle,
		"-B" + gccLib,
		"-L", gccLib,
		"-isystem", cxx,
		"-isystem", cxxTarget,
		"-isystem", include,
		"-L", lib,
		"--target=x86_64-w64-mingw32",
		"hello.cc",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("argv mismatch:\n got: %#v\nwant: %#v", got, want)
	}
}

func TestInjectBundledTargetRuntimeRespectsExplicitSysroot(t *testing.T) {
	root := t.TempDir()
	sdk := filepath.Join(root, "sysroot", "x86_64-apple-darwin")
	mustMkdirAll(t, filepath.Join(sdk, "usr", "include"))
	mustWriteFile(t, filepath.Join(sdk, "usr", "include", "stdio.h"))

	withBundleRoots(t, root)

	argv := []string{"cleng", "--target=x86_64-apple-darwin", "-isysroot", "/custom/sdk", "hello.c"}
	got := injectBundledTargetRuntime(argv)
	if !reflect.DeepEqual(got, argv) {
		t.Fatalf("argv should have been left alone:\n got: %#v\nwant: %#v", got, argv)
	}
}

func withBundleRoots(t *testing.T, root string) {
	t.Helper()
	prev := bundleRootsFunc
	bundleRootsFunc = func() []string { return []string{root} }
	t.Cleanup(func() { bundleRootsFunc = prev })
}

func mustMkdirAll(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(path, 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", path, err)
	}
}

func mustWriteFile(t *testing.T, path string) {
	t.Helper()
	if err := os.WriteFile(path, []byte{}, 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}
