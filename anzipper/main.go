package main

import (
	"archive/zip"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	zipPath := flag.String("zip", "", "zip archive to inspect or extract")
	outDir := flag.String("out", "", "output directory for extraction")
	listOnly := flag.Bool("list", false, "list archive entries instead of extracting")
	flag.Parse()

	if *zipPath == "" {
		flag.Usage()
		os.Exit(2)
	}

	if err := run(*zipPath, *outDir, *listOnly); err != nil {
		fmt.Fprintf(os.Stderr, "anzipper: %v\n", err)
		os.Exit(1)
	}
}

func run(zipPath, outDir string, listOnly bool) error {
	reader, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("open %s: %w", zipPath, err)
	}
	defer reader.Close()

	if listOnly {
		for _, file := range reader.File {
			fmt.Printf("%12d  %s\n", file.UncompressedSize64, file.Name)
		}
		return nil
	}

	if outDir == "" {
		outDir = defaultOutputDir(zipPath)
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return fmt.Errorf("create output directory %s: %w", outDir, err)
	}

	var extracted int
	for _, file := range reader.File {
		if err := extractFile(file, outDir); err != nil {
			return err
		}
		if !file.FileInfo().IsDir() {
			extracted++
		}
	}

	fmt.Printf("extracted %d file(s) into %s\n", extracted, outDir)
	return nil
}

func defaultOutputDir(zipPath string) string {
	base := strings.TrimSuffix(filepath.Base(zipPath), filepath.Ext(zipPath))
	if base == "" || base == "." || base == string(filepath.Separator) {
		return "unzipped"
	}
	return base
}

func extractFile(file *zip.File, outputDir string) error {
	targetPath := filepath.Join(outputDir, filepath.FromSlash(file.Name))
	inside, err := isWithinBase(outputDir, targetPath)
	if err != nil {
		return fmt.Errorf("validate extraction path for %s: %w", file.Name, err)
	}
	if !inside {
		return fmt.Errorf("refusing to extract %s outside %s", file.Name, outputDir)
	}

	mode := file.Mode()
	switch {
	case file.FileInfo().IsDir():
		return os.MkdirAll(targetPath, 0o755)
	case mode&os.ModeSymlink != 0:
		return fmt.Errorf("refusing to extract symlink %s", file.Name)
	case !mode.IsRegular():
		return fmt.Errorf("refusing to extract unsupported entry %s", file.Name)
	}

	if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
		return fmt.Errorf("create parent directory for %s: %w", file.Name, err)
	}

	reader, err := file.Open()
	if err != nil {
		return fmt.Errorf("open archived file %s: %w", file.Name, err)
	}
	defer reader.Close()

	if mode == 0 {
		mode = 0o644
	}

	writer, err := os.OpenFile(targetPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode)
	if err != nil {
		return fmt.Errorf("create output file %s: %w", targetPath, err)
	}
	defer writer.Close()

	if _, err := io.Copy(writer, reader); err != nil {
		return fmt.Errorf("extract %s: %w", file.Name, err)
	}

	return nil
}

func isWithinBase(basePath, targetPath string) (bool, error) {
	basePath = filepath.Clean(basePath)
	targetPath = filepath.Clean(targetPath)

	rel, err := filepath.Rel(basePath, targetPath)
	if err != nil {
		return false, err
	}
	if rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
		return false, nil
	}
	if rel == "." {
		return false, errors.New("archive entry resolves to the output directory itself")
	}
	return true, nil
}
