package main

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
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

func TestCreateSourceArchiveSkipsGeneratedOutputsButKeepsBuildMetadata(t *testing.T) {
	t.Parallel()

	root := t.TempDir()
	mustWriteTestFile(t, filepath.Join(root, "TriangleApp.cpp"), "int main() { return 0; }\n")
	mustWriteTestFile(t, filepath.Join(root, "bin", "keep.txt"), "checked-in asset\n")
	mustWriteTestFile(t, filepath.Join(root, ".cliant-cmake", "TriangleCpp", "CMakeFiles", "rules.ninja"), "rule cc\n")
	mustWriteTestFile(t, filepath.Join(root, ".cliant-cmake", "TriangleCpp", "bin", "gameos.xvd"), strings.Repeat("x", 1024))
	mustWriteTestFile(t, filepath.Join(root, ".cliant-cmake", "TriangleCpp", "CMakeFiles", "TriangleCpp.dir", "TriangleApp.cpp.obj"), "stale object\n")

	archivePath, err := createSourceArchive(root)
	if err != nil {
		t.Fatalf("createSourceArchive returned error: %v", err)
	}
	defer os.Remove(archivePath)

	archive, err := zip.OpenReader(archivePath)
	if err != nil {
		t.Fatalf("open archive: %v", err)
	}
	defer archive.Close()

	names := make(map[string]bool, len(archive.File))
	for _, file := range archive.File {
		names[file.Name] = true
	}

	for _, want := range []string{
		"TriangleApp.cpp",
		"bin/keep.txt",
		".cliant-cmake/TriangleCpp/CMakeFiles/rules.ninja",
	} {
		if !names[want] {
			t.Fatalf("archive missing %q; names=%v", want, names)
		}
	}

	for _, unwanted := range []string{
		".cliant-cmake/TriangleCpp/bin/gameos.xvd",
		".cliant-cmake/TriangleCpp/CMakeFiles/TriangleCpp.dir/TriangleApp.cpp.obj",
	} {
		if names[unwanted] {
			t.Fatalf("archive unexpectedly included %q", unwanted)
		}
	}
}

func TestRelayHandleBuildRejectsOversizedUploadClearly(t *testing.T) {
	t.Parallel()

	var body bytes.Buffer
	writer := multipart.NewWriter(&body)

	requestPayload, err := json.Marshal(buildRequest{
		Target:     "TriangleCpp",
		OutputName: "TriangleCpp.exe",
		GOOS:       "windows",
		GOARCH:     "amd64",
		CGOEnabled: "1",
		Language:   "cpp",
	})
	if err != nil {
		t.Fatalf("marshal request: %v", err)
	}
	if err := writer.WriteField("request", string(requestPayload)); err != nil {
		t.Fatalf("write request field: %v", err)
	}

	part, err := writer.CreateFormFile("archive", "source.zip")
	if err != nil {
		t.Fatalf("create archive part: %v", err)
	}
	if _, err := part.Write(bytes.Repeat([]byte("x"), 2*1024*1024)); err != nil {
		t.Fatalf("write archive part: %v", err)
	}
	if err := writer.Close(); err != nil {
		t.Fatalf("close multipart body: %v", err)
	}

	request := httptest.NewRequest(http.MethodPost, "/build", &body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	recorder := httptest.NewRecorder()

	relay := &relayServer{
		jobs:   make(chan *relayJob, 1),
		active: make(map[string]*relayJob),
	}
	relay.handleBuild(1)(recorder, request)

	if recorder.Code != http.StatusRequestEntityTooLarge {
		t.Fatalf("status = %d, want %d; body=%s", recorder.Code, http.StatusRequestEntityTooLarge, recorder.Body.String())
	}
	if !strings.Contains(recorder.Body.String(), "source archive exceeds 1 MiB upload limit") {
		t.Fatalf("response body = %q", recorder.Body.String())
	}
}

func mustWriteTestFile(t *testing.T, path, contents string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}
