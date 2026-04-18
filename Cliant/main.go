package main

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"mime/multipart"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

var errUsageRequested = errors.New("usage requested")

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

type buildOptions struct {
	serverURL  string
	sourcePath string
	outputPath string
	target     string
	goos       string
	goarch     string
	cgoEnabled string
	timeout    time.Duration
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
		return errors.New("missing server URL")
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
	case "health":
		return runHealth(serverURL)
	case "-h", "--help", "help":
		printUsage()
		return nil
	default:
		printUsage()
		return fmt.Errorf("unknown command %q", command)
	}
}

func parseBuildOptions(serverURL string, args []string) (buildOptions, error) {
	opts := buildOptions{
		serverURL:  serverURL,
		sourcePath: ".",
		goos:       runtime.GOOS,
		goarch:     runtime.GOARCH,
		cgoEnabled: "0",
		timeout:    10 * time.Minute,
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
		default:
			if strings.HasPrefix(arg, "-") {
				return opts, fmt.Errorf("unknown flag %q", arg)
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

	return opts, nil
}

func runBuild(opts buildOptions) error {
	archivePath, err := createSourceArchive(opts.sourcePath)
	if err != nil {
		return err
	}
	defer os.Remove(archivePath)

	requestBody, contentType, err := makeMultipartBody(archivePath, buildRequest{
		Target:     opts.target,
		OutputName: filepath.Base(opts.outputPath),
		GOOS:       opts.goos,
		GOARCH:     opts.goarch,
		CGOEnabled: opts.cgoEnabled,
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
			name := info.Name()
			if shouldSkip(name, info.IsDir()) {
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

func shouldSkip(name string, isDir bool) bool {
	switch name {
	case ".git", ".pycache", "build":
		return true
	case ".DS_Store":
		return true
	}
	if isDir && strings.HasPrefix(name, ".") && name != "." {
		return false
	}
	return false
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

func decodeServerError(response *http.Response) error {
	payload, _ := io.ReadAll(response.Body)
	var structured errorResponse
	if json.Unmarshal(payload, &structured) == nil && structured.Error != "" {
		if structured.Log != "" {
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
	fmt.Fprintln(os.Stderr, "  cliant <server-url> build [path] [-o output] [-pkg target] [-goos os] [-goarch arch] [-cgo 0|1] [-timeout 10m]")
	fmt.Fprintln(os.Stderr, "  cliant <server-url> health")
}

func init() {
	mime.AddExtensionType(".exe", "application/octet-stream")
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
