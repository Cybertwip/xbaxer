package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"mime"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strings"
	"time"
)

func main() {
	sourceURL := flag.String("url", "", "HTTP or HTTPS URL to download")
	outputPath := flag.String("o", "", "output file path")
	timeout := flag.Duration("timeout", 30*time.Second, "request timeout")
	flag.Parse()

	if *sourceURL == "" {
		flag.Usage()
		os.Exit(2)
	}

	if err := run(*sourceURL, *outputPath, *timeout); err != nil {
		fmt.Fprintf(os.Stderr, "gatter: %v\n", err)
		os.Exit(1)
	}
}

func run(sourceURL, outputPath string, timeout time.Duration) error {
	parsedURL, err := url.Parse(sourceURL)
	if err != nil {
		return fmt.Errorf("parse URL: %w", err)
	}
	switch parsedURL.Scheme {
	case "http", "https":
	default:
		return errUnsupportedScheme
	}

	request, err := http.NewRequest(http.MethodGet, sourceURL, nil)
	if err != nil {
		return fmt.Errorf("build request: %w", err)
	}
	request.Header.Set("User-Agent", "gatter/1.0")

	client := &http.Client{Timeout: timeout}
	response, err := client.Do(request)
	if err != nil {
		return fmt.Errorf("download %s: %w", sourceURL, err)
	}
	defer response.Body.Close()

	if response.StatusCode < http.StatusOK || response.StatusCode >= http.StatusMultipleChoices {
		snippet, _ := io.ReadAll(io.LimitReader(response.Body, 512))
		return fmt.Errorf("unexpected status %s: %s", response.Status, strings.TrimSpace(string(snippet)))
	}

	if outputPath == "" {
		outputPath = inferOutputPath(sourceURL, response.Header.Get("Content-Disposition"))
	}

	if err := os.MkdirAll(filepath.Dir(outputPath), 0o755); err != nil {
		return fmt.Errorf("create output directory for %s: %w", outputPath, err)
	}

	file, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("create %s: %w", outputPath, err)
	}
	defer file.Close()

	written, err := io.Copy(file, response.Body)
	if err != nil {
		return fmt.Errorf("write %s: %w", outputPath, err)
	}

	fmt.Printf("downloaded %s -> %s (%d bytes)\n", sourceURL, outputPath, written)
	return nil
}

func inferOutputPath(sourceURL, contentDisposition string) string {
	if name := filenameFromContentDisposition(contentDisposition); name != "" {
		return name
	}

	parsedURL, err := url.Parse(sourceURL)
	if err == nil {
		if base := sanitizeFilename(path.Base(parsedURL.Path)); base != "" {
			return base
		}
	}

	return "download.bin"
}

func filenameFromContentDisposition(contentDisposition string) string {
	if contentDisposition == "" {
		return ""
	}

	_, params, err := mime.ParseMediaType(contentDisposition)
	if err != nil {
		return ""
	}

	for _, key := range []string{"filename", "filename*"} {
		if name := sanitizeFilename(params[key]); name != "" {
			return name
		}
	}

	return ""
}

func sanitizeFilename(name string) string {
	name = strings.TrimSpace(name)
	if name == "" {
		return ""
	}

	if strings.HasPrefix(strings.ToLower(name), "utf-8''") {
		decoded, err := url.QueryUnescape(name[len("utf-8''"):])
		if err == nil {
			name = decoded
		}
	}

	name = filepath.Base(strings.ReplaceAll(name, "\\", "/"))
	switch name {
	case "", ".", "/", "\\":
		return ""
	}
	return name
}

var errUnsupportedScheme = errors.New("only http and https URLs are supported")

func init() {
	flag.CommandLine.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), "Usage: gatter -url <http(s)://...> [-o output]\n\n")
		flag.PrintDefaults()
	}
}
