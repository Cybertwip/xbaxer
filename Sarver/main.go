package main

import (
	"archive/zip"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"mime/multipart"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

type buildRequest struct {
	Target     string `json:"target"`
	OutputName string `json:"output_name"`
	GOOS       string `json:"goos"`
	GOARCH     string `json:"goarch"`
	CGOEnabled string `json:"cgo_enabled"`
}

type errorResponse struct {
	Error string `json:"error"`
	Log   string `json:"log,omitempty"`
}

type server struct {
	goBinary   string
	goCacheDir string
	goModCache string
	goPathDir  string
	timeout    time.Duration
}

func main() {
	listenAddr := flag.String("listen", "0.0.0.0:17777", "listen address")
	goBinary := flag.String("go", defaultGoBinaryPath(), "path to the Go executable used for builds")
	cacheRoot := flag.String("cache-dir", defaultCacheRoot(), "directory used for Go build and module caches")
	timeout := flag.Duration("timeout", 10*time.Minute, "maximum time allowed per build")
	maxUploadMiB := flag.Int64("max-upload-mib", 256, "maximum accepted source archive size in MiB")
	flag.Parse()

	cacheRootPath := *cacheRoot
	goCacheDir := filepath.Join(cacheRootPath, "cache")
	goModCache := filepath.Join(cacheRootPath, "mod")
	goPathDir := filepath.Join(cacheRootPath, "path")
	for _, dir := range []string{goCacheDir, goModCache, goPathDir} {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			log.Fatalf("create cache directory %s: %v", dir, err)
		}
	}

	srv := &server{
		goBinary:   *goBinary,
		goCacheDir: goCacheDir,
		goModCache: goModCache,
		goPathDir:  goPathDir,
		timeout:    *timeout,
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", srv.handleHealth)
	mux.HandleFunc("/build", srv.handleBuild(*maxUploadMiB))

	httpServer := &http.Server{
		Addr:              *listenAddr,
		Handler:           loggingMiddleware(mux),
		ReadHeaderTimeout: 5 * time.Second,
	}

	log.Printf("sarver listen address: http://%s", *listenAddr)
	log.Printf("sarver go binary: %s", *goBinary)
	log.Printf("sarver cache root: %s", cacheRootPath)
	if isLoopbackListenAddr(*listenAddr) {
		log.Printf("sarver warning: %s is loopback-only; remote hosts will not be able to connect", *listenAddr)
	} else {
		log.Printf("sarver network access enabled; connect from your host using http://<server-ip>:%s", listenPort(*listenAddr))
	}
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		recorder := &responseRecorder{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(recorder, r)
		log.Printf("%s %s from %s -> %d (%s)", r.Method, r.URL.Path, r.RemoteAddr, recorder.status, time.Since(start).Round(time.Millisecond))
	})
}

func (s *server) handleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}

	writeJSON(w, http.StatusOK, map[string]string{
		"status": "ok",
	})
}

func (s *server) handleBuild(maxUploadMiB int64) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
			return
		}

		maxBytes := maxUploadMiB * 1024 * 1024
		r.Body = http.MaxBytesReader(w, r.Body, maxBytes)
		if err := r.ParseMultipartForm(32 << 20); err != nil {
			writeJSON(w, http.StatusBadRequest, errorResponse{Error: fmt.Sprintf("invalid multipart request: %v", err)})
			return
		}

		req, err := decodeBuildRequest(r.FormValue("request"))
		if err != nil {
			writeJSON(w, http.StatusBadRequest, errorResponse{Error: err.Error()})
			return
		}

		archiveFile, _, err := r.FormFile("archive")
		if err != nil {
			writeJSON(w, http.StatusBadRequest, errorResponse{Error: fmt.Sprintf("missing archive: %v", err)})
			return
		}
		defer archiveFile.Close()

		workspace, err := os.MkdirTemp("", "sarver-build-*")
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("create temp workspace: %v", err)})
			return
		}
		defer os.RemoveAll(workspace)

		sourceDir := filepath.Join(workspace, "src")
		if err := os.MkdirAll(sourceDir, 0o755); err != nil {
			writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("create source workspace: %v", err)})
			return
		}

		if err := extractArchive(archiveFile, sourceDir); err != nil {
			writeJSON(w, http.StatusBadRequest, errorResponse{Error: fmt.Sprintf("extract archive: %v", err)})
			return
		}

		outputName := req.OutputName
		if outputName == "" {
			outputName = defaultOutputName(req.Target, req.GOOS)
		} else {
			outputName, err = sanitizeOutputName(outputName, req.GOOS)
			if err != nil {
				writeJSON(w, http.StatusBadRequest, errorResponse{Error: err.Error()})
				return
			}
		}
		outputPath := filepath.Join(workspace, outputName)

		ctx, cancel := context.WithTimeout(r.Context(), s.timeout)
		defer cancel()

		args := []string{"build", "-buildvcs=false", "-trimpath", "-o", outputPath}
		target, err := sanitizeBuildTarget(req.Target)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, errorResponse{Error: err.Error()})
			return
		}
		log.Printf("build request from %s: target=%s output=%s goos=%s goarch=%s cgo=%s", r.RemoteAddr, target, outputName, req.GOOS, req.GOARCH, req.CGOEnabled)
		args = append(args, target)

		cmd := exec.CommandContext(ctx, s.goBinary, args...)
		cmd.Dir = sourceDir
		cmd.Env = append(os.Environ(),
			"GOOS="+req.GOOS,
			"GOARCH="+req.GOARCH,
			"CGO_ENABLED="+req.CGOEnabled,
			"GOTOOLCHAIN=local",
			"GOCACHE="+s.goCacheDir,
			"GOMODCACHE="+s.goModCache,
			"GOPATH="+s.goPathDir,
		)
		buildLog, err := cmd.CombinedOutput()
		if err != nil {
			status := http.StatusInternalServerError
			if errors.Is(ctx.Err(), context.DeadlineExceeded) {
				status = http.StatusGatewayTimeout
			}
			log.Printf("build failed for %s: %v", outputName, err)
			if len(buildLog) > 0 {
				log.Printf("build log for %s:\n%s", outputName, strings.TrimSpace(string(buildLog)))
			}
			writeJSON(w, status, errorResponse{
				Error: fmt.Sprintf("build failed: %v", err),
				Log:   string(buildLog),
			})
			return
		}

		builtFile, err := os.Open(outputPath)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("open build output: %v", err)})
			return
		}
		defer builtFile.Close()

		stat, err := builtFile.Stat()
		if err == nil {
			w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", outputName))
		w.Header().Set("X-Sarver-Output-Name", outputName)
		w.Header().Set("X-Sarver-Goos", req.GOOS)
		w.Header().Set("X-Sarver-Goarch", req.GOARCH)
		w.WriteHeader(http.StatusOK)
		log.Printf("build succeeded for %s; streaming result to %s", outputName, r.RemoteAddr)

		if _, err := io.Copy(w, builtFile); err != nil {
			log.Printf("stream build output: %v", err)
		}
	}
}

type responseRecorder struct {
	http.ResponseWriter
	status int
}

func (r *responseRecorder) WriteHeader(status int) {
	r.status = status
	r.ResponseWriter.WriteHeader(status)
}

func decodeBuildRequest(raw string) (buildRequest, error) {
	var req buildRequest
	if raw == "" {
		return req, errors.New("missing build request metadata")
	}
	if err := json.Unmarshal([]byte(raw), &req); err != nil {
		return req, fmt.Errorf("decode build request: %w", err)
	}
	if req.GOOS == "" || req.GOARCH == "" {
		return req, errors.New("goos and goarch are required")
	}
	if req.CGOEnabled == "" {
		req.CGOEnabled = "0"
	}
	return req, nil
}

func extractArchive(reader multipart.File, destination string) error {
	tempFile, err := os.CreateTemp(destination, "archive-*.zip")
	if err != nil {
		return err
	}
	defer os.Remove(tempFile.Name())

	if _, err := io.Copy(tempFile, reader); err != nil {
		tempFile.Close()
		return err
	}
	if err := tempFile.Close(); err != nil {
		return err
	}

	zipReader, err := zip.OpenReader(tempFile.Name())
	if err != nil {
		return err
	}
	defer zipReader.Close()

	for _, file := range zipReader.File {
		targetPath := filepath.Join(destination, filepath.FromSlash(file.Name))
		if !isWithinBase(destination, targetPath) {
			return fmt.Errorf("refusing to extract %s outside workspace", file.Name)
		}

		if file.FileInfo().IsDir() {
			if err := os.MkdirAll(targetPath, 0o755); err != nil {
				return err
			}
			continue
		}
		if file.Mode()&os.ModeSymlink != 0 {
			return fmt.Errorf("refusing to extract symlink %s", file.Name)
		}

		if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
			return err
		}

		source, err := file.Open()
		if err != nil {
			return err
		}

		mode := file.Mode()
		if mode == 0 {
			mode = 0o644
		}
		target, err := os.OpenFile(targetPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode)
		if err != nil {
			source.Close()
			return err
		}

		if _, err := io.Copy(target, source); err != nil {
			target.Close()
			source.Close()
			return err
		}
		target.Close()
		source.Close()
	}

	return nil
}

func isWithinBase(basePath, targetPath string) bool {
	basePath = filepath.Clean(basePath)
	targetPath = filepath.Clean(targetPath)
	rel, err := filepath.Rel(basePath, targetPath)
	if err != nil {
		return false
	}
	return rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator))
}

func defaultOutputName(target, goos string) string {
	base := strings.TrimSpace(filepath.Base(target))
	switch base {
	case "", ".", string(filepath.Separator):
		base = "remote-build"
	}
	if goos == "windows" && !strings.HasSuffix(strings.ToLower(base), ".exe") {
		base += ".exe"
	}
	return base
}

func sanitizeOutputName(outputName, goos string) (string, error) {
	cleanName := strings.TrimSpace(outputName)
	if cleanName == "" {
		return "", errors.New("output name must not be empty")
	}

	cleanName = filepath.Base(filepath.Clean(filepath.FromSlash(cleanName)))
	switch cleanName {
	case "", ".", string(filepath.Separator):
		return "", errors.New("output name must be a file name")
	}

	if goos == "windows" && !strings.HasSuffix(strings.ToLower(cleanName), ".exe") {
		cleanName += ".exe"
	}
	return cleanName, nil
}

func sanitizeBuildTarget(target string) (string, error) {
	cleanTarget := strings.TrimSpace(target)
	if cleanTarget == "" || cleanTarget == "." {
		return ".", nil
	}

	cleanTarget = filepath.Clean(filepath.FromSlash(cleanTarget))
	if filepath.IsAbs(cleanTarget) {
		return "", errors.New("build target must be relative to the uploaded workspace")
	}

	relTarget, err := filepath.Rel(".", cleanTarget)
	if err != nil {
		return "", fmt.Errorf("resolve build target: %w", err)
	}
	if relTarget == ".." || strings.HasPrefix(relTarget, ".."+string(filepath.Separator)) {
		return "", errors.New("build target must stay within the uploaded workspace")
	}

	return filepath.ToSlash(cleanTarget), nil
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}

func defaultCacheRoot() string {
	return filepath.Join(os.TempDir(), "sarver-go")
}

func defaultGoBinaryPath() string {
	executablePath, err := os.Executable()
	if err == nil {
		executableDir := filepath.Dir(executablePath)
		candidates := []string{
			filepath.Join(executableDir, "..", "campiler", "bin", "go.exe"),
			filepath.Join(executableDir, "..", "campiler", "bin", "go"),
		}
		for _, candidate := range candidates {
			if info, statErr := os.Stat(candidate); statErr == nil && !info.IsDir() {
				return candidate
			}
		}
	}
	return "go"
}

func isLoopbackListenAddr(listenAddr string) bool {
	host, _, err := net.SplitHostPort(listenAddr)
	if err != nil {
		return false
	}
	host = strings.Trim(host, "[]")
	if host == "" || host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func listenPort(listenAddr string) string {
	_, port, err := net.SplitHostPort(listenAddr)
	if err != nil || port == "" {
		return "17777"
	}
	return port
}
