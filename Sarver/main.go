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
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

type buildRequest struct {
	Target     string `json:"target"`
	OutputName string `json:"output_name"`
	GOOS       string `json:"goos"`
	GOARCH     string `json:"goarch"`
	CGOEnabled string `json:"cgo_enabled"`
	// Language selects the build dispatcher. "" / "go" runs `go build`
	// against the uploaded module (the historic xbax behaviour).
	// "c" / "cpp" / "c++" runs cleng directly against the uploaded
	// source tree, which is how raw C/C++ work gets distributed onto
	// the console without involving a Go module.
	Language string `json:"language,omitempty"`
	// CompilerArgs are extra flags forwarded to cleng when Language is
	// c/cpp. Things like -std=c++20, -O2, -DFOO, -I., -L., -lwhatever.
	// Ignored for Language == go (use cgo CFLAGS in the source for
	// that case).
	CompilerArgs []string `json:"compiler_args,omitempty"`
	// Sources is an optional explicit list of source files (relative
	// to the uploaded archive root) to compile. When empty in c/cpp
	// mode, sarver auto-discovers every *.c/*.cc/*.cpp/*.cxx file in
	// the archive.
	Sources []string `json:"sources,omitempty"`
	// Steps is an ordered compiler/linker replay plan generated on the
	// host after a local CMake/Ninja configure. Sarver executes every
	// step inside the same extracted workspace so intermediate objects,
	// import libraries, and final binaries stay available for later
	// link stages.
	Steps []buildStep `json:"steps,omitempty"`
	// ArtifactPath selects which file from the plan workspace should be
	// streamed back to the host after all steps succeed.
	ArtifactPath string `json:"artifact_path,omitempty"`
}

type buildStep struct {
	Args             []string `json:"args"`
	WorkingDirectory string   `json:"working_directory,omitempty"`
}

type errorResponse struct {
	Error string `json:"error"`
	Log   string `json:"log,omitempty"`
}

type reverseJob struct {
	ID         string       `json:"id"`
	Request    buildRequest `json:"request"`
	ArchiveURL string       `json:"archive_url"`
	ResultURL  string       `json:"result_url"`
	ErrorURL   string       `json:"error_url"`
}

type server struct {
	goBinary    string
	clengBinary string
	goCacheDir  string
	goModCache  string
	goPathDir   string
	timeout     time.Duration
	reverseURL  string
}

type buildExecution struct {
	OutputName string
	OutputPath string
	Workspace  string
}

func main() {
	// Subcommand sugar: `sarver.exe reverse <url> [flags...]` is equivalent
	// to `sarver.exe -reverse <url> [flags...]`. Same for `probe`. We
	// rewrite os.Args before flag.Parse so the rest of the code path is
	// unchanged.
	if len(os.Args) >= 2 {
		switch os.Args[1] {
		case "reverse":
			if len(os.Args) < 3 || strings.HasPrefix(os.Args[2], "-") {
				fmt.Fprintln(os.Stderr, "usage: sarver.exe reverse <relay-url> [flags...]")
				os.Exit(2)
			}
			rest := append([]string{os.Args[0], "-reverse", os.Args[2]}, os.Args[3:]...)
			os.Args = rest
		case "probe":
			rest := append([]string{os.Args[0], "-probe"}, os.Args[2:]...)
			os.Args = rest
		}
	}

	listenAddr := flag.String("listen", "0.0.0.0:17777", "listen address")
	reverseURL := flag.String("reverse", "", "reverse relay URL to pull build jobs from")
	probeMode := flag.Bool("probe", false, "run the Xbox-firewall probe (binds every allowlisted port and logs inbound connections); pair with `cliant probe <xbox-ip>` from the host")
	goBinary := flag.String("go", defaultGoBinaryPath(), "path to the Go executable used for builds")
	clengBinary := flag.String("cleng", defaultClengBinaryPath(), "path to cleng.exe (used as CC/CXX for cgo C/C++ compilation); empty disables")
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
		goBinary:    *goBinary,
		clengBinary: *clengBinary,
		goCacheDir:  goCacheDir,
		goModCache:  goModCache,
		goPathDir:   goPathDir,
		timeout:     *timeout,
		reverseURL:  normalizeReverseURL(*reverseURL),
	}

	log.Printf("sarver go binary: %s", *goBinary)
	if *clengBinary != "" {
		log.Printf("sarver cleng binary (CC/CXX for cgo): %s", *clengBinary)
	} else {
		log.Printf("sarver cleng binary: <disabled> — cgo C/C++ builds will fall back to whatever CC/CXX the environment provides")
	}
	log.Printf("sarver cache root: %s", cacheRootPath)

	if *probeMode {
		if err := runProbe(); err != nil {
			log.Fatal(err)
		}
		return
	}

	if srv.reverseURL != "" {
		log.Printf("sarver reverse mode enabled; relay=%s", srv.reverseURL)
		log.Printf("sarver will pull build jobs from the host relay")
		if err := srv.runReverseLoop(); err != nil {
			log.Fatal(err)
		}
		return
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
	if isLoopbackListenAddr(*listenAddr) {
		log.Printf("sarver warning: %s is loopback-only; remote hosts will not be able to connect", *listenAddr)
	} else {
		log.Printf("sarver network access enabled; connect from your host using http://<server-ip>:%s", listenPort(*listenAddr))
	}
	if err := ensureFirewallRule(listenPort(*listenAddr)); err == nil && runtime.GOOS == "windows" {
		log.Printf("sarver firewall rule ready for TCP port %s", listenPort(*listenAddr))
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

		execResult, status, errResp := s.executeBuild(r.Context(), req, archiveFile, r.RemoteAddr)
		if errResp != nil {
			writeJSON(w, status, *errResp)
			return
		}
		defer execResult.cleanup()

		builtFile, err := os.Open(execResult.OutputPath)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("open build output: %v", err)})
			return
		}
		defer builtFile.Close()

		if stat, err := builtFile.Stat(); err == nil {
			w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", execResult.OutputName))
		w.Header().Set("X-Sarver-Output-Name", execResult.OutputName)
		w.Header().Set("X-Sarver-Goos", req.GOOS)
		w.Header().Set("X-Sarver-Goarch", req.GOARCH)
		w.WriteHeader(http.StatusOK)
		log.Printf("build succeeded for %s; streaming result to %s", execResult.OutputName, r.RemoteAddr)

		if _, err := io.Copy(w, builtFile); err != nil {
			log.Printf("stream build output: %v", err)
		}
	}
}

func (s *server) runReverseLoop() error {
	client := &http.Client{Timeout: 45 * time.Second}
	for {
		job, err := s.pullReverseJob(client)
		if err != nil {
			log.Printf("reverse poll error: %v", err)
			time.Sleep(3 * time.Second)
			continue
		}
		if job == nil {
			continue
		}
		s.handleReverseJob(client, *job)
	}
}

func (s *server) pullReverseJob(client *http.Client) (*reverseJob, error) {
	resp, err := client.Get(s.reverseURL + "/reverse/pull")
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	switch resp.StatusCode {
	case http.StatusNoContent:
		return nil, nil
	case http.StatusOK:
		var job reverseJob
		if err := json.NewDecoder(resp.Body).Decode(&job); err != nil {
			return nil, fmt.Errorf("decode reverse job: %w", err)
		}
		return &job, nil
	default:
		payload, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("reverse pull failed: %s %s", resp.Status, strings.TrimSpace(string(payload)))
	}
}

func (s *server) handleReverseJob(client *http.Client, job reverseJob) {
	log.Printf("reverse build job received: id=%s target=%s output=%s goos=%s goarch=%s", job.ID, job.Request.Target, job.Request.OutputName, job.Request.GOOS, job.Request.GOARCH)

	resp, err := client.Get(job.ArchiveURL)
	if err != nil {
		s.postReverseError(client, job, http.StatusBadGateway, errorResponse{Error: fmt.Sprintf("download archive: %v", err)})
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		payload, _ := io.ReadAll(resp.Body)
		s.postReverseError(client, job, http.StatusBadGateway, errorResponse{
			Error: fmt.Sprintf("download archive failed: %s", resp.Status),
			Log:   strings.TrimSpace(string(payload)),
		})
		return
	}

	execResult, status, errResp := s.executeBuild(context.Background(), job.Request, resp.Body, "reverse:"+job.ID)
	if errResp != nil {
		s.postReverseError(client, job, status, *errResp)
		return
	}
	defer execResult.cleanup()

	if err := s.postReverseResult(client, job, execResult); err != nil {
		log.Printf("reverse result upload failed for %s: %v", job.ID, err)
	}
}

func (s *server) postReverseResult(client *http.Client, job reverseJob, execResult *buildExecution) error {
	file, err := os.Open(execResult.OutputPath)
	if err != nil {
		return fmt.Errorf("open build output: %w", err)
	}
	defer file.Close()

	req, err := http.NewRequest(http.MethodPost, job.ResultURL, file)
	if err != nil {
		return fmt.Errorf("create result request: %w", err)
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	req.Header.Set("X-Sarver-Output-Name", execResult.OutputName)

	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("post result: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		payload, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("post result failed: %s %s", resp.Status, strings.TrimSpace(string(payload)))
	}

	log.Printf("reverse build job %s completed successfully", job.ID)
	return nil
}

func (s *server) postReverseError(client *http.Client, job reverseJob, status int, payload errorResponse) {
	body, _ := json.Marshal(payload)
	req, err := http.NewRequest(http.MethodPost, job.ErrorURL, strings.NewReader(string(body)))
	if err != nil {
		log.Printf("create reverse error request for %s: %v", job.ID, err)
		return
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Sarver-Status", fmt.Sprintf("%d", status))

	resp, err := client.Do(req)
	if err != nil {
		log.Printf("post reverse error for %s: %v", job.ID, err)
		return
	}
	defer resp.Body.Close()
	log.Printf("reverse build job %s failed: %s", job.ID, payload.Error)
}

func (s *server) executeBuild(parent context.Context, req buildRequest, archiveReader io.Reader, requester string) (*buildExecution, int, *errorResponse) {
	workspace, err := os.MkdirTemp("", "sarver-build-*")
	if err != nil {
		return nil, http.StatusInternalServerError, &errorResponse{Error: fmt.Sprintf("create temp workspace: %v", err)}
	}

	sourceDir := filepath.Join(workspace, "src")
	if err := os.MkdirAll(sourceDir, 0o755); err != nil {
		_ = os.RemoveAll(workspace)
		return nil, http.StatusInternalServerError, &errorResponse{Error: fmt.Sprintf("create source workspace: %v", err)}
	}

	if err := extractArchive(archiveReader, sourceDir); err != nil {
		_ = os.RemoveAll(workspace)
		return nil, http.StatusBadRequest, &errorResponse{Error: fmt.Sprintf("extract archive: %v", err)}
	}

	outputName := req.OutputName
	if outputName == "" {
		outputName = defaultOutputName(req.Target, req.GOOS)
	} else {
		outputName, err = sanitizeOutputName(outputName, req.GOOS)
		if err != nil {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: err.Error()}
		}
	}

	outputPath := filepath.Join(workspace, outputName)
	if strings.TrimSpace(req.ArtifactPath) != "" {
		outputPath, err = resolveWorkspacePath(sourceDir, req.ArtifactPath)
		if err != nil {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: err.Error()}
		}
	}

	ctx, cancel := context.WithTimeout(parent, s.timeout)
	defer cancel()

	lang := normalizeLanguage(req.Language)
	log.Printf("build request from %s: lang=%s target=%s output=%s goos=%s goarch=%s cgo=%s", requester, lang, req.Target, outputName, req.GOOS, req.GOARCH, req.CGOEnabled)

	var (
		cmd      *exec.Cmd
		buildLog []byte
	)
	switch lang {
	case "go":
		target, terr := sanitizeBuildTarget(req.Target)
		if terr != nil {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: terr.Error()}
		}
		cmd = s.buildGoCommand(ctx, sourceDir, outputPath, target, req)
	case "c", "cpp":
		if s.clengBinary == "" {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: "language=" + lang + " requires cleng to be configured (-cleng path)"}
		}
		var cerr error
		cmd, cerr = s.buildClengCommand(ctx, sourceDir, outputPath, lang, req)
		if cerr != nil {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: cerr.Error()}
		}
	case "plan":
		if s.clengBinary == "" {
			_ = os.RemoveAll(workspace)
			return nil, http.StatusBadRequest, &errorResponse{Error: "language=plan requires cleng to be configured (-cleng path)"}
		}
		cmd = nil
	default:
		_ = os.RemoveAll(workspace)
		return nil, http.StatusBadRequest, &errorResponse{Error: fmt.Sprintf("unknown language %q (want go, c, cpp, or plan)", req.Language)}
	}

	if lang == "plan" {
		buildLog, err = s.executeBuildPlan(ctx, sourceDir, req)
	} else {
		buildLog, err = cmd.CombinedOutput()
	}
	if err != nil {
		status := http.StatusInternalServerError
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			status = http.StatusGatewayTimeout
		}
		log.Printf("build failed for %s: %v", outputName, err)
		if len(buildLog) > 0 {
			log.Printf("build log for %s:\n%s", outputName, strings.TrimSpace(string(buildLog)))
		}
		_ = os.RemoveAll(workspace)
		return nil, status, &errorResponse{
			Error: fmt.Sprintf("build failed: %v", err),
			Log:   string(buildLog),
		}
	}

	return &buildExecution{
		OutputName: outputName,
		OutputPath: outputPath,
		Workspace:  workspace,
	}, http.StatusOK, nil
}

func (e *buildExecution) cleanup() {
	if e != nil && e.Workspace != "" {
		_ = os.RemoveAll(e.Workspace)
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

func extractArchive(reader io.Reader, destination string) error {
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

// defaultClengBinaryPath looks for cleng.exe next to sarver in the
// package layout produced by `cmake --build build --target package-xbax`:
//
//	package/Xbax/sarver/bin/sarver.exe
//	package/Xbax/cleng/bin/cleng.exe
//
// Returns an empty string if cleng is not present so cgo builds can still
// proceed with whatever CC/CXX is on PATH (or fail loudly if there is none).
func defaultClengBinaryPath() string {
	executablePath, err := os.Executable()
	if err != nil {
		return ""
	}
	executableDir := filepath.Dir(executablePath)
	candidates := []string{
		filepath.Join(executableDir, "..", "cleng", "bin", "cleng.exe"),
		filepath.Join(executableDir, "..", "cleng", "bin", "cleng"),
	}
	for _, candidate := range candidates {
		if info, statErr := os.Stat(candidate); statErr == nil && !info.IsDir() {
			return candidate
		}
	}
	return ""
}

// normalizeReverseURL accepts the relay address in the forms users
// actually type into a console: `http://host:port`, `https://host:port`,
// `host:port`, and even the common typo `http:host:port` (missing
// slashes). Returns "" unchanged so callers can detect "not in reverse
// mode".
func normalizeReverseURL(raw string) string {
	v := strings.TrimSpace(raw)
	if v == "" {
		return ""
	}
	for _, scheme := range []string{"http", "https"} {
		prefix := scheme + ":"
		if strings.HasPrefix(v, prefix) && !strings.HasPrefix(v, prefix+"//") {
			v = scheme + "://" + strings.TrimPrefix(v, prefix)
		}
	}
	if !strings.Contains(v, "://") {
		v = "http://" + v
	}
	return strings.TrimRight(v, "/")
}

// llvmTripleFor maps Go's GOOS/GOARCH onto an LLVM target triple suitable
// for clang's `--target=` flag. Returns "" when the combination has no
// well-known triple, in which case sarver leaves cleng on its default
// (mingw) triple. Triples are kept minimal — clang fills in the rest from
// its built-in defaults.
func llvmTripleFor(goos, goarch string) string {
	arch := goarch
	switch arch {
	case "amd64":
		arch = "x86_64"
	case "386":
		arch = "i386"
	case "arm64":
		arch = "aarch64"
	}
	switch goos {
	case "darwin":
		return arch + "-apple-darwin"
	case "linux":
		return arch + "-linux-gnu"
	case "windows":
		return arch + "-w64-mingw32"
	case "freebsd", "openbsd", "netbsd":
		return arch + "-unknown-" + goos
	default:
		return ""
	}
}

// normalizeLanguage canonicalises the optional Language field of a build
// request. Empty defaults to "go" for backward compatibility with the
// pre-multi-language protocol; "c++"/"cxx" alias to "cpp".
func normalizeLanguage(lang string) string {
	switch strings.ToLower(strings.TrimSpace(lang)) {
	case "", "go":
		return "go"
	case "c":
		return "c"
	case "cpp", "c++", "cxx":
		return "cpp"
	case "plan", "cmake":
		return "plan"
	default:
		return strings.ToLower(strings.TrimSpace(lang))
	}
}

// buildGoCommand assembles the historic `go build` invocation. Extracted
// from executeBuild so the language dispatcher in executeBuild can stay
// readable.
func (s *server) buildGoCommand(ctx context.Context, sourceDir, outputPath, target string, req buildRequest) *exec.Cmd {
	args := []string{"build", "-buildvcs=false", "-trimpath", "-o", outputPath, target}
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
	if s.clengBinary != "" && req.CGOEnabled == "1" {
		// cleng.exe is a Go-fronted Clang driver. Wire it as CC/CXX
		// when cgo is enabled — it's the only C/C++ toolchain
		// available on the console. Translate GOOS/GOARCH into an LLVM
		// target triple so the same cleng binary can emit PE for
		// windows, Mach-O for darwin and ELF for linux. Without
		// `--target=`, cleng would default to mingw and reject
		// darwin's `-arch <goarch>` cgo injection.
		cmd.Env = append(cmd.Env,
			"CC="+s.clengBinary,
			"CXX="+s.clengBinary,
		)
		if triple := llvmTripleFor(req.GOOS, req.GOARCH); triple != "" {
			targetFlag := "--target=" + triple
			cmd.Env = append(cmd.Env,
				"CGO_CFLAGS="+targetFlag,
				"CGO_CXXFLAGS="+targetFlag,
				"CGO_LDFLAGS="+targetFlag,
			)
		}
	}
	return cmd
}

// buildClengCommand assembles a direct cleng invocation for distributed
// C/C++ work. Source files come either from req.Sources (an explicit list
// of archive-relative paths) or, if that list is empty, from a recursive
// scan of the workspace for .c/.cc/.cpp/.cxx files. Extra compiler args
// (req.CompilerArgs) are appended verbatim — that is where the developer
// passes -std=c++20, -O2, -DFOO, -I., -L., -lwhatever, etc.
//
// We deliberately keep the wire protocol simple: one input archive plus
// flags, one output binary. Multi-stage builds (CMake/Make/Ninja) are out
// of scope — the developer can either pre-process those locally and ship a
// single-stage compile, or wrap them in a Go cgo build and use the Go
// dispatcher above.
func (s *server) buildClengCommand(ctx context.Context, sourceDir, outputPath, lang string, req buildRequest) (*exec.Cmd, error) {
	sources, err := resolveSources(sourceDir, req.Sources, lang)
	if err != nil {
		return nil, err
	}
	if len(sources) == 0 {
		return nil, fmt.Errorf("no %s source files found in upload", lang)
	}

	args := make([]string, 0, len(sources)+len(req.CompilerArgs)+8)

	// xLang tells cleng to treat positional inputs as the requested
	// language regardless of file extension. Useful when the caller
	// uploads .h/.inc files with explicit Sources.
	xLang := "c"
	if lang == "cpp" {
		xLang = "c++"
	}
	args = append(args, "-x", xLang)

	if triple := llvmTripleFor(req.GOOS, req.GOARCH); triple != "" {
		args = append(args, "--target="+triple)
	}

	args = append(args, req.CompilerArgs...)
	args = append(args, "-o", outputPath)
	args = append(args, sources...)

	cmd := exec.CommandContext(ctx, s.clengBinary, args...)
	cmd.Dir = sourceDir
	cmd.Env = os.Environ()
	return cmd, nil
}

// resolveSources turns the protocol's optional Sources list into absolute
// paths inside sourceDir, validating that each entry stays within the
// workspace. When Sources is empty, walks sourceDir for files matching
// the requested language's extensions.
func resolveSources(sourceDir string, requested []string, lang string) ([]string, error) {
	if len(requested) > 0 {
		out := make([]string, 0, len(requested))
		for _, rel := range requested {
			clean := filepath.Clean(filepath.FromSlash(rel))
			if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") {
				return nil, fmt.Errorf("source path %q must be relative to the upload root", rel)
			}
			abs := filepath.Join(sourceDir, clean)
			if !isWithinBase(sourceDir, abs) {
				return nil, fmt.Errorf("source path %q escapes the upload root", rel)
			}
			if _, err := os.Stat(abs); err != nil {
				return nil, fmt.Errorf("source %q: %w", rel, err)
			}
			out = append(out, abs)
		}
		return out, nil
	}

	exts := map[string]bool{".c": true}
	if lang == "cpp" {
		exts = map[string]bool{".cc": true, ".cpp": true, ".cxx": true, ".c++": true, ".C": true}
	}
	var out []string
	walkErr := filepath.Walk(sourceDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		if exts[filepath.Ext(path)] {
			out = append(out, path)
		}
		return nil
	})
	if walkErr != nil {
		return nil, fmt.Errorf("scan sources: %w", walkErr)
	}
	return out, nil
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

func ensureFirewallRule(port string) error {
	if runtime.GOOS != "windows" || port == "" {
		return nil
	}

	netshPath, err := exec.LookPath("netsh")
	if err != nil {
		return nil
	}

	ruleName := fmt.Sprintf("Sarver-TCP-%s", port)

	deleteCmd := exec.Command(
		netshPath, "advfirewall", "firewall", "delete", "rule",
		fmt.Sprintf("name=%s", ruleName),
		"protocol=TCP",
		fmt.Sprintf("localport=%s", port),
	)
	_ = deleteCmd.Run()

	addCmd := exec.Command(
		netshPath, "advfirewall", "firewall", "add", "rule",
		fmt.Sprintf("name=%s", ruleName),
		"dir=in",
		"action=allow",
		"protocol=TCP",
		fmt.Sprintf("localport=%s", port),
		"profile=any",
		"enable=yes",
	)
	_, err = addCmd.CombinedOutput()
	return err
}
