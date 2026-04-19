package main

import (
	"archive/zip"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"mime"
	"mime/multipart"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

const relayPullTimeout = 25 * time.Second

var errUsageRequested = errors.New("usage requested")

type buildRequest struct {
	Target       string      `json:"target"`
	OutputName   string      `json:"output_name"`
	GOOS         string      `json:"goos"`
	GOARCH       string      `json:"goarch"`
	CGOEnabled   string      `json:"cgo_enabled"`
	Language     string      `json:"language,omitempty"`
	CompilerArgs []string    `json:"compiler_args,omitempty"`
	Sources      []string    `json:"sources,omitempty"`
	Steps        []buildStep `json:"steps,omitempty"`
	ArtifactPath string      `json:"artifact_path,omitempty"`
}

type buildStep struct {
	Args             []string `json:"args"`
	WorkingDirectory string   `json:"working_directory,omitempty"`
}

type errorResponse struct {
	Error string `json:"error"`
	Log   string `json:"log,omitempty"`
}

type buildOptions struct {
	serverURL    string
	sourcePath   string
	outputPath   string
	target       string
	goos         string
	goarch       string
	cgoEnabled   string
	timeout      time.Duration
	language     string
	compilerArgs []string
	sources      []string
}

type serveOptions struct {
	listenAddr   string
	maxUploadMiB int64
}

type reverseJobPayload struct {
	ID         string       `json:"id"`
	Request    buildRequest `json:"request"`
	ArchiveURL string       `json:"archive_url"`
	ResultURL  string       `json:"result_url"`
	ErrorURL   string       `json:"error_url"`
}

type relayResult struct {
	Status       int
	ArtifactPath string
	ErrorPayload errorResponse
}

type relayJob struct {
	ID          string
	Request     buildRequest
	ArchivePath string
	OutputName  string
	Result      chan relayResult
}

type relayServer struct {
	jobs chan *relayJob

	mu     sync.Mutex
	active map[string]*relayJob
}

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintf(os.Stderr, "cliant: %v\n", err)
		os.Exit(1)
	}
}

func run(args []string) error {
	if len(args) == 0 {
		printUsage()
		return errors.New("missing command or server URL")
	}

	switch args[0] {
	case "serve":
		opts, err := parseServeOptions(args[1:])
		if err != nil {
			if errors.Is(err, errUsageRequested) {
				return nil
			}
			return err
		}
		return runServe(opts)
	case "probe":
		return runProbe(args[1:])
	case "-h", "--help", "help":
		printUsage()
		return nil
	}

	if len(args) == 1 {
		printUsage()
		return errors.New("missing command")
	}

	serverURL := strings.TrimRight(args[0], "/")
	command := args[1]

	switch command {
	case "build":
		opts, err := parseBuildOptions(serverURL, args[2:])
		if err != nil {
			if errors.Is(err, errUsageRequested) {
				return nil
			}
			return err
		}
		return runBuild(opts)
	case "cmake-build":
		opts, err := parseCMakeBuildOptions(serverURL, args[2:])
		if err != nil {
			if errors.Is(err, errUsageRequested) {
				return nil
			}
			return err
		}
		return runCMakeBuild(opts)
	case "health":
		return runHealth(serverURL)
	default:
		printUsage()
		return fmt.Errorf("unknown command %q", command)
	}
}

func parseServeOptions(args []string) (serveOptions, error) {
	opts := serveOptions{
		listenAddr:   "0.0.0.0:17777",
		maxUploadMiB: 256,
	}

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-h", "--help":
			printUsage()
			return opts, errUsageRequested
		case "-listen":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -listen")
			}
			opts.listenAddr = args[i]
		case "-max-upload-mib":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -max-upload-mib")
			}
			value, err := strconv.ParseInt(args[i], 10, 64)
			if err != nil || value <= 0 {
				return opts, errors.New("max upload size must be a positive integer")
			}
			opts.maxUploadMiB = value
		default:
			return opts, fmt.Errorf("unknown serve flag %q", args[i])
		}
	}

	return opts, nil
}

func parseBuildOptions(serverURL string, args []string) (buildOptions, error) {
	opts := buildOptions{
		serverURL:  serverURL,
		sourcePath: ".",
		goos:       runtime.GOOS,
		goarch:     runtime.GOARCH,
		cgoEnabled: "1",
		// cgo defaults to ON because most builds pull in cleng-driven C/C++
		// wrappers. Override with -cgo 0 for pure-Go builds, and -goos /
		// -goarch when you really mean to cross-compile.
		timeout: 10 * time.Minute,
	}

	var sourceSet bool
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "-h", "--help":
			printUsage()
			return opts, errUsageRequested
		case "-o", "--output":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -o")
			}
			opts.outputPath = args[i]
		case "-pkg", "--pkg":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -pkg")
			}
			opts.target = args[i]
		case "-goos":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -goos")
			}
			opts.goos = args[i]
		case "-goarch":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -goarch")
			}
			opts.goarch = args[i]
		case "-cgo":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -cgo")
			}
			opts.cgoEnabled = normalizeCGO(args[i])
		case "-lang", "--lang":
			// Selects the dispatcher on the sarver side. "go" runs
			// `go build`; "c"/"cpp" runs cleng directly against the
			// uploaded source tree. Auto-detected from the source
			// extension when omitted.
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -lang")
			}
			opts.language = args[i]
		case "-X", "--compiler-arg":
			// Repeatable. Forwarded verbatim to cleng for c/cpp builds
			// (e.g. -X -std=c++20 -X -O2 -X -DFOO=1 -X -lkernel32).
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -X")
			}
			opts.compilerArgs = append(opts.compilerArgs, args[i])
		case "-src", "--source":
			// Repeatable. Explicit upload-relative source files passed
			// to cleng. When unset, sarver auto-discovers all matching
			// source files in the upload root.
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -src")
			}
			opts.sources = append(opts.sources, args[i])
		case "-timeout":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -timeout")
			}
			timeout, err := time.ParseDuration(args[i])
			if err != nil {
				return opts, fmt.Errorf("parse timeout: %w", err)
			}
			opts.timeout = timeout
		case "--":
			opts.compilerArgs = append(opts.compilerArgs, args[i+1:]...)
			i = len(args)
		default:
			if strings.HasPrefix(arg, "-") {
				opts.compilerArgs = append(opts.compilerArgs, arg)
				if compilerArgConsumesNextValue(arg) {
					i++
					if i >= len(args) {
						return opts, fmt.Errorf("missing value for compiler flag %q", arg)
					}
					opts.compilerArgs = append(opts.compilerArgs, args[i])
				}
				continue
			}
			if sourceSet {
				return opts, fmt.Errorf("unexpected extra path %q", arg)
			}
			opts.sourcePath = arg
			sourceSet = true
		}
	}

	if opts.serverURL == "" {
		return opts, errors.New("server URL is required")
	}

	parsedURL, err := url.Parse(opts.serverURL)
	if err != nil {
		return opts, fmt.Errorf("parse server URL: %w", err)
	}
	if parsedURL.Scheme != "http" && parsedURL.Scheme != "https" {
		return opts, errors.New("server URL must use http or https")
	}

	info, err := os.Stat(opts.sourcePath)
	if err != nil {
		return opts, fmt.Errorf("stat %s: %w", opts.sourcePath, err)
	}

	if opts.target == "" {
		if info.IsDir() {
			opts.target = "."
		} else {
			opts.target = filepath.Base(opts.sourcePath)
			opts.sourcePath = filepath.Dir(opts.sourcePath)
		}
	}

	if opts.outputPath == "" {
		opts.outputPath = defaultOutputPath(opts.sourcePath, opts.target, info, opts.goos)
	}

	if opts.language == "" {
		opts.language = detectLanguage(opts.sourcePath, opts.target, info)
	}
	if len(opts.compilerArgs) != 0 && !isCompilerLanguage(opts.language) {
		return opts, fmt.Errorf(
			"compiler flags were provided but build language resolved to %q; use -lang c/cpp or point cliant at a C/C++ source tree",
			opts.language,
		)
	}

	return opts, nil
}

func compilerArgConsumesNextValue(arg string) bool {
	switch arg {
	case "-I",
		"-L",
		"-D",
		"-U",
		"-F",
		"-Xclang",
		"-Xlinker",
		"-Xassembler",
		"-include",
		"-imacros",
		"-idirafter",
		"-iframework",
		"-isystem",
		"-iquote",
		"-isysroot",
		"-syslibroot",
		"-stdlib",
		"-std",
		"-target",
		"--target",
		"-arch",
		"-x":
		return true
	default:
		return false
	}
}

func isCompilerLanguage(lang string) bool {
	switch strings.ToLower(lang) {
	case "c", "cpp", "c++", "cc", "cxx":
		return true
	default:
		return false
	}
}

func runServe(opts serveOptions) error {
	relay := &relayServer{
		jobs:   make(chan *relayJob, 16),
		active: make(map[string]*relayJob),
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", relay.handleHealth)
	mux.HandleFunc("/build", relay.handleBuild(opts.maxUploadMiB))
	mux.HandleFunc("/reverse/pull", relay.handleReversePull)
	mux.HandleFunc("/reverse/archive/", relay.handleReverseArchive)
	mux.HandleFunc("/reverse/result/", relay.handleReverseResult)
	mux.HandleFunc("/reverse/error/", relay.handleReverseError)

	server := &http.Server{
		Addr:              opts.listenAddr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	log.Printf("cliant relay listening on http://%s", opts.listenAddr)
	log.Printf("cliant relay waiting for reverse workers from remote sarver instances")
	return server.ListenAndServe()
}

func runBuild(opts buildOptions) error {
	archivePath, err := createSourceArchive(opts.sourcePath)
	if err != nil {
		return err
	}
	defer os.Remove(archivePath)

	requestBody, contentType, err := makeMultipartBody(archivePath, buildRequest{
		Target:       opts.target,
		OutputName:   filepath.Base(opts.outputPath),
		GOOS:         opts.goos,
		GOARCH:       opts.goarch,
		CGOEnabled:   opts.cgoEnabled,
		Language:     opts.language,
		CompilerArgs: opts.compilerArgs,
		Sources:      opts.sources,
	})
	if err != nil {
		return err
	}

	client := &http.Client{Timeout: opts.timeout}
	request, err := http.NewRequest(http.MethodPost, opts.serverURL+"/build", requestBody)
	if err != nil {
		return fmt.Errorf("create build request: %w", err)
	}
	request.Header.Set("Content-Type", contentType)

	response, err := client.Do(request)
	if err != nil {
		return fmt.Errorf("request build: %w", err)
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusOK {
		return decodeServerError(response)
	}

	targetPath := opts.outputPath
	if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
		return fmt.Errorf("create output directory: %w", err)
	}

	output, err := os.Create(targetPath)
	if err != nil {
		return fmt.Errorf("create %s: %w", targetPath, err)
	}
	defer output.Close()

	written, err := io.Copy(output, response.Body)
	if err != nil {
		return fmt.Errorf("save build output: %w", err)
	}

	fmt.Printf("saved %s (%d bytes)\n", targetPath, written)
	return nil
}

func runHealth(serverURL string) error {
	response, err := http.Get(strings.TrimRight(serverURL, "/") + "/healthz")
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return decodeServerError(response)
	}
	_, err = io.Copy(os.Stdout, response.Body)
	return err
}

func (s *relayServer) handleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *relayServer) handleBuild(maxUploadMiB int64) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
			return
		}

		maxBytes := maxUploadMiB * 1024 * 1024
		r.Body = http.MaxBytesReader(w, r.Body, maxBytes)

		req, archivePath, status, errResp := receiveBuildRequest(r, maxUploadMiB)
		if errResp != nil {
			writeJSON(w, status, *errResp)
			return
		}

		job, err := s.newJob(req, archivePath)
		if err != nil {
			_ = os.Remove(archivePath)
			writeJSON(w, http.StatusServiceUnavailable, errorResponse{Error: err.Error()})
			return
		}
		defer s.cleanupJob(job)

		log.Printf("queued reverse build job %s: target=%s output=%s goos=%s goarch=%s", job.ID, req.Target, job.OutputName, req.GOOS, req.GOARCH)

		select {
		case result := <-job.Result:
			if result.ArtifactPath != "" {
				defer os.Remove(result.ArtifactPath)

				builtFile, err := os.Open(result.ArtifactPath)
				if err != nil {
					writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("open build output: %v", err)})
					return
				}
				defer builtFile.Close()

				if stat, err := builtFile.Stat(); err == nil {
					w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
				}
				w.Header().Set("Content-Type", "application/octet-stream")
				w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", job.OutputName))
				w.WriteHeader(http.StatusOK)
				if _, err := io.Copy(w, builtFile); err != nil {
					log.Printf("stream relay build output: %v", err)
				}
				return
			}

			status := result.Status
			if status == 0 {
				status = http.StatusInternalServerError
			}
			writeJSON(w, status, result.ErrorPayload)
		case <-r.Context().Done():
			writeJSON(w, http.StatusRequestTimeout, errorResponse{Error: "build request canceled while waiting for reverse worker"})
		}
	}
}

func (s *relayServer) handleReversePull(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}

	deadline := time.Now().Add(relayPullTimeout)
	for time.Now().Before(deadline) {
		wait := time.Until(deadline)
		if wait <= 0 {
			break
		}

		select {
		case job := <-s.jobs:
			if !s.hasJob(job.ID) {
				continue
			}

			baseURL := baseURLFromRequest(r)
			payload := reverseJobPayload{
				ID:         job.ID,
				Request:    job.Request,
				ArchiveURL: baseURL + "/reverse/archive/" + job.ID,
				ResultURL:  baseURL + "/reverse/result/" + job.ID,
				ErrorURL:   baseURL + "/reverse/error/" + job.ID,
			}
			log.Printf("dispatching reverse build job %s", job.ID)
			writeJSON(w, http.StatusOK, payload)
			return
		case <-time.After(minDuration(wait, 2*time.Second)):
		}
	}

	w.WriteHeader(http.StatusNoContent)
}

func (s *relayServer) handleReverseArchive(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}

	job := s.getJob(strings.TrimPrefix(r.URL.Path, "/reverse/archive/"))
	if job == nil {
		writeJSON(w, http.StatusNotFound, errorResponse{Error: "unknown job"})
		return
	}

	archiveFile, err := os.Open(job.ArchivePath)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("open archive: %v", err)})
		return
	}
	defer archiveFile.Close()

	if stat, err := archiveFile.Stat(); err == nil {
		w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
	}
	w.Header().Set("Content-Type", "application/zip")
	if _, err := io.Copy(w, archiveFile); err != nil {
		log.Printf("stream reverse archive for %s: %v", job.ID, err)
	}
}

func (s *relayServer) handleReverseResult(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}

	job := s.getJob(strings.TrimPrefix(r.URL.Path, "/reverse/result/"))
	if job == nil {
		writeJSON(w, http.StatusNotFound, errorResponse{Error: "unknown job"})
		return
	}

	tempFile, err := os.CreateTemp("", "cliant-result-*")
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errorResponse{Error: fmt.Sprintf("create temp result: %v", err)})
		return
	}
	defer tempFile.Close()

	if _, err := io.Copy(tempFile, r.Body); err != nil {
		_ = os.Remove(tempFile.Name())
		writeJSON(w, http.StatusBadRequest, errorResponse{Error: fmt.Sprintf("read result: %v", err)})
		return
	}

	if err := s.completeJob(job.ID, relayResult{
		Status:       http.StatusOK,
		ArtifactPath: tempFile.Name(),
	}); err != nil {
		_ = os.Remove(tempFile.Name())
		writeJSON(w, http.StatusGone, errorResponse{Error: err.Error()})
		return
	}

	log.Printf("reverse build job %s returned artifact", job.ID)
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *relayServer) handleReverseError(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, errorResponse{Error: "method not allowed"})
		return
	}

	jobID := strings.TrimPrefix(r.URL.Path, "/reverse/error/")
	if s.getJob(jobID) == nil {
		writeJSON(w, http.StatusNotFound, errorResponse{Error: "unknown job"})
		return
	}

	payload, _ := io.ReadAll(r.Body)
	status := http.StatusInternalServerError
	if rawStatus := strings.TrimSpace(r.Header.Get("X-Sarver-Status")); rawStatus != "" {
		if parsed, err := strconv.Atoi(rawStatus); err == nil {
			status = parsed
		}
	}

	var errPayload errorResponse
	if json.Unmarshal(payload, &errPayload) != nil || errPayload.Error == "" {
		errPayload = errorResponse{Error: strings.TrimSpace(string(payload))}
		if errPayload.Error == "" {
			errPayload.Error = "reverse build failed"
		}
	}

	if err := s.completeJob(jobID, relayResult{
		Status:       status,
		ErrorPayload: errPayload,
	}); err != nil {
		writeJSON(w, http.StatusGone, errorResponse{Error: err.Error()})
		return
	}

	log.Printf("reverse build job %s returned error: %s", jobID, errPayload.Error)
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *relayServer) newJob(req buildRequest, archivePath string) (*relayJob, error) {
	job := &relayJob{
		ID:          newJobID(),
		Request:     req,
		ArchivePath: archivePath,
		OutputName:  req.OutputName,
		Result:      make(chan relayResult, 1),
	}
	if job.OutputName == "" {
		job.OutputName = defaultOutputPath(".", req.Target, nil, req.GOOS)
	}

	s.mu.Lock()
	s.active[job.ID] = job
	s.mu.Unlock()

	select {
	case s.jobs <- job:
		return job, nil
	default:
		s.mu.Lock()
		delete(s.active, job.ID)
		s.mu.Unlock()
		return nil, errors.New("reverse build queue is full; start sarver in reverse mode first")
	}
}

func (s *relayServer) cleanupJob(job *relayJob) {
	s.mu.Lock()
	delete(s.active, job.ID)
	s.mu.Unlock()
	_ = os.Remove(job.ArchivePath)
}

func (s *relayServer) getJob(jobID string) *relayJob {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.active[jobID]
}

func (s *relayServer) hasJob(jobID string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, ok := s.active[jobID]
	return ok
}

func (s *relayServer) completeJob(jobID string, result relayResult) error {
	s.mu.Lock()
	job, ok := s.active[jobID]
	s.mu.Unlock()
	if !ok || job == nil {
		return errors.New("job is no longer waiting for a result")
	}

	select {
	case job.Result <- result:
		return nil
	default:
		return errors.New("job result channel is no longer available")
	}
}

func receiveBuildRequest(r *http.Request, maxUploadMiB int64) (buildRequest, string, int, *errorResponse) {
	if err := r.ParseMultipartForm(32 << 20); err != nil {
		var maxErr *http.MaxBytesError
		if errors.As(err, &maxErr) {
			return buildRequest{}, "", http.StatusRequestEntityTooLarge, &errorResponse{
				Error: fmt.Sprintf("source archive exceeds %d MiB upload limit", maxUploadMiB),
			}
		}
		return buildRequest{}, "", http.StatusBadRequest, &errorResponse{Error: fmt.Sprintf("invalid multipart request: %v", err)}
	}

	req, err := decodeBuildRequest(r.FormValue("request"))
	if err != nil {
		return buildRequest{}, "", http.StatusBadRequest, &errorResponse{Error: err.Error()}
	}

	archiveFile, _, err := r.FormFile("archive")
	if err != nil {
		return buildRequest{}, "", http.StatusBadRequest, &errorResponse{Error: fmt.Sprintf("missing archive: %v", err)}
	}
	defer archiveFile.Close()

	tempFile, err := os.CreateTemp("", "cliant-relay-*.zip")
	if err != nil {
		return buildRequest{}, "", http.StatusInternalServerError, &errorResponse{Error: fmt.Sprintf("create temp archive: %v", err)}
	}
	defer tempFile.Close()

	if _, err := io.Copy(tempFile, archiveFile); err != nil {
		_ = os.Remove(tempFile.Name())
		return buildRequest{}, "", http.StatusBadRequest, &errorResponse{Error: fmt.Sprintf("save archive: %v", err)}
	}

	return req, tempFile.Name(), http.StatusOK, nil
}

func createSourceArchive(sourcePath string) (string, error) {
	sourcePath, err := filepath.Abs(sourcePath)
	if err != nil {
		return "", fmt.Errorf("resolve source path: %w", err)
	}

	info, err := os.Stat(sourcePath)
	if err != nil {
		return "", err
	}

	tempFile, err := os.CreateTemp("", "cliant-source-*.zip")
	if err != nil {
		return "", err
	}
	defer tempFile.Close()

	zipWriter := zip.NewWriter(tempFile)
	if info.IsDir() {
		err = filepath.Walk(sourcePath, func(path string, info os.FileInfo, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			relPath, err := filepath.Rel(sourcePath, path)
			if err != nil {
				return err
			}
			if relPath == "." {
				return nil
			}
			if shouldSkipArchivePath(relPath, info) {
				if info.IsDir() {
					return filepath.SkipDir
				}
				return nil
			}
			if info.Mode()&os.ModeSymlink != 0 {
				return nil
			}
			if info.IsDir() {
				return nil
			}
			return addFileToZip(zipWriter, path, relPath, info.Mode())
		})
	} else {
		err = addFileToZip(zipWriter, sourcePath, filepath.Base(sourcePath), info.Mode())
	}
	if err != nil {
		zipWriter.Close()
		os.Remove(tempFile.Name())
		return "", err
	}

	if err := zipWriter.Close(); err != nil {
		os.Remove(tempFile.Name())
		return "", err
	}

	return tempFile.Name(), nil
}

func addFileToZip(zipWriter *zip.Writer, sourcePath, archivePath string, mode os.FileMode) error {
	header := &zip.FileHeader{
		Name:     filepath.ToSlash(archivePath),
		Method:   zip.Deflate,
		Modified: time.Now(),
	}
	header.SetMode(mode)

	writer, err := zipWriter.CreateHeader(header)
	if err != nil {
		return err
	}

	file, err := os.Open(sourcePath)
	if err != nil {
		return err
	}
	defer file.Close()

	_, err = io.Copy(writer, file)
	return err
}

func shouldSkipArchivePath(relPath string, info os.FileInfo) bool {
	name := info.Name()
	isDir := info.IsDir()
	switch name {
	case ".git", ".hg", ".svn", ".pycache", "__pycache__", "build", ".DS_Store":
		return true
	}
	if isDir && strings.HasPrefix(name, "cmake-build-") {
		return true
	}

	parts := strings.Split(filepath.ToSlash(relPath), "/")
	if underGeneratedBuildTree(parts) && containsBuildOutputComponent(parts) {
		return true
	}
	if underGeneratedBuildTree(parts) && shouldSkipGeneratedBuildFile(name, info) {
		return true
	}

	return false
}

func underGeneratedBuildTree(parts []string) bool {
	for _, part := range parts {
		switch {
		case part == ".cliant-cmake", part == "build", part == "CMakeFiles":
			return true
		case strings.HasPrefix(part, "cmake-build-"):
			return true
		}
	}
	return false
}

func containsBuildOutputComponent(parts []string) bool {
	for _, part := range parts {
		switch part {
		case "bin", "lib", "obj", "objs":
			return true
		}
	}
	return false
}

func shouldSkipGeneratedBuildFile(name string, info os.FileInfo) bool {
	if info.IsDir() {
		return false
	}

	switch strings.ToLower(filepath.Ext(name)) {
	case ".a", ".appx", ".dll", ".dylib", ".exe", ".ilk", ".lib", ".obj", ".o", ".pdb", ".so", ".xvd":
		return true
	default:
		return false
	}
}

func makeMultipartBody(archivePath string, request buildRequest) (io.Reader, string, error) {
	archiveFile, err := os.Open(archivePath)
	if err != nil {
		return nil, "", fmt.Errorf("open archive: %w", err)
	}

	pipeReader, pipeWriter := io.Pipe()
	writer := multipart.NewWriter(pipeWriter)

	go func() {
		defer archiveFile.Close()
		defer pipeWriter.Close()
		defer writer.Close()

		payload, err := json.Marshal(request)
		if err != nil {
			pipeWriter.CloseWithError(err)
			return
		}
		if err := writer.WriteField("request", string(payload)); err != nil {
			pipeWriter.CloseWithError(err)
			return
		}

		part, err := writer.CreateFormFile("archive", "source.zip")
		if err != nil {
			pipeWriter.CloseWithError(err)
			return
		}
		if _, err := io.Copy(part, archiveFile); err != nil {
			pipeWriter.CloseWithError(err)
			return
		}
	}()

	return pipeReader, writer.FormDataContentType(), nil
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

func decodeServerError(response *http.Response) error {
	payload, _ := io.ReadAll(response.Body)
	var structured errorResponse
	if json.Unmarshal(payload, &structured) == nil && structured.Error != "" {
		if structured.Log != "" {
			// Surface the remote build log on stderr immediately so the user
			// sees the compiler/cgo diagnostics even if the wrapping caller
			// only inspects the exit code. We still embed it in the returned
			// error so anything that captures stderr+err sees it once.
			fmt.Fprintln(os.Stderr, "--- remote build log ---")
			fmt.Fprintln(os.Stderr, strings.TrimSpace(structured.Log))
			fmt.Fprintln(os.Stderr, "------------------------")
			return fmt.Errorf("%s\n%s", structured.Error, structured.Log)
		}
		return errors.New(structured.Error)
	}
	if len(payload) == 0 {
		return fmt.Errorf("server returned %s", response.Status)
	}
	return fmt.Errorf("server returned %s: %s", response.Status, strings.TrimSpace(string(payload)))
}

func normalizeCGO(value string) string {
	switch strings.ToLower(strings.TrimSpace(value)) {
	case "1", "true", "yes", "on":
		return "1"
	default:
		return "0"
	}
}

func printUsage() {
	fmt.Fprintln(os.Stderr, "Usage:")
	fmt.Fprintln(os.Stderr, "  cliant serve [-listen 0.0.0.0:17777] [-max-upload-mib 256]")
	fmt.Fprintln(os.Stderr, "  cliant probe <xbox-ip>")
	fmt.Fprintln(os.Stderr, "  cliant <server-url> build [path] [-o output] [-pkg target] [-goos os] [-goarch arch] [-cgo 0|1] [-timeout 10m] [-- compiler-flags]")
	fmt.Fprintln(os.Stderr, "  cliant <server-url> cmake-build [source-dir] -target <cmake-target> [-toolchain file] [-build-dir dir] [-o output] [-- cmake-configure-flags]")
	fmt.Fprintln(os.Stderr, "  cliant <server-url> health")
}

func init() {
	mime.AddExtensionType(".exe", "application/octet-stream")
}

// detectLanguage decides whether to dispatch the build as Go or C/C++
// based on what the user pointed cliant at. The rules are intentionally
// simple — explicit `-lang` always wins, this is just the auto-detect:
//   - if the source path is a single file, use its extension;
//   - otherwise scan the source directory for go.mod first (Go),
//     then for any .cpp/.cc/.cxx (cpp), then .c (c);
//   - default to "go" for backward compatibility.
func detectLanguage(sourcePath, target string, info os.FileInfo) string {
	// Single-file builds are unambiguous.
	if info != nil && !info.IsDir() {
		switch strings.ToLower(filepath.Ext(sourcePath)) {
		case ".go":
			return "go"
		case ".c":
			return "c"
		case ".cpp", ".cc", ".cxx", ".c++":
			return "cpp"
		}
	}
	if target != "" && target != "." {
		switch strings.ToLower(filepath.Ext(target)) {
		case ".go":
			return "go"
		case ".c":
			return "c"
		case ".cpp", ".cc", ".cxx", ".c++":
			return "cpp"
		}
	}
	root := sourcePath
	if info != nil && !info.IsDir() {
		root = filepath.Dir(sourcePath)
	}
	if _, err := os.Stat(filepath.Join(root, "go.mod")); err == nil {
		return "go"
	}
	hasCpp, hasC := false, false
	_ = filepath.Walk(root, func(path string, fi os.FileInfo, err error) error {
		if err != nil || fi.IsDir() {
			return nil
		}
		switch strings.ToLower(filepath.Ext(path)) {
		case ".cpp", ".cc", ".cxx", ".c++":
			hasCpp = true
		case ".c":
			hasC = true
		}
		return nil
	})
	switch {
	case hasCpp:
		return "cpp"
	case hasC:
		return "c"
	default:
		return "go"
	}
}

func defaultOutputPath(sourcePath, target string, sourceInfo os.FileInfo, goos string) string {
	base := "remote-build"

	if target != "" && target != "." {
		base = filepath.Base(filepath.Clean(target))
		if ext := filepath.Ext(base); ext == ".go" {
			base = strings.TrimSuffix(base, ext)
		}
	} else if sourceInfo != nil && sourceInfo.IsDir() {
		if absSourcePath, err := filepath.Abs(sourcePath); err == nil {
			base = filepath.Base(absSourcePath)
		}
	}

	if base == "" || base == "." || base == string(filepath.Separator) {
		base = "remote-build"
	}
	if goos == "windows" && !strings.HasSuffix(strings.ToLower(base), ".exe") {
		base += ".exe"
	}
	return base
}

func newJobID() string {
	randomBytes := make([]byte, 8)
	if _, err := rand.Read(randomBytes); err != nil {
		return fmt.Sprintf("job-%d", time.Now().UnixNano())
	}
	return hex.EncodeToString(randomBytes)
}

func baseURLFromRequest(r *http.Request) string {
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	return scheme + "://" + r.Host
}

func minDuration(a, b time.Duration) time.Duration {
	if a < b {
		return a
	}
	return b
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}
