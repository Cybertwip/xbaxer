//go:build windows

package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"
)

const (
	fileAttributeReadonly     = 0x00000001
	fileAttributeDirectory    = 0x00000010
	fileAttributeNormal       = 0x00000080
	fileAttributeReparsePoint = 0x00000400
)

func main() {
	targetPath := flag.String("path", "", "directory or file to remove recursively")
	flag.Parse()

	if *targetPath == "" {
		flag.Usage()
		os.Exit(2)
	}

	removed, err := run(*targetPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "cleener: %v\n", err)
		os.Exit(1)
	}
	if removed {
		fmt.Printf("removed %s\n", displayPath(mustLongPath(*targetPath)))
	}
}

func run(target string) (bool, error) {
	longPath, err := validateTarget(target)
	if err != nil {
		return false, err
	}

	removed, err := removeTree(longPath)
	if err != nil {
		return false, err
	}
	return removed, nil
}

func validateTarget(target string) (string, error) {
	target = strings.TrimSpace(target)
	if target == "" {
		return "", errors.New("path is required")
	}

	absTarget, err := filepath.Abs(target)
	if err != nil {
		return "", fmt.Errorf("resolve %s: %w", target, err)
	}
	absTarget = filepath.Clean(absTarget)

	volume := filepath.VolumeName(absTarget)
	remainder := strings.TrimLeft(strings.TrimPrefix(absTarget, volume), `\/`)
	if volume != "" && remainder == "" {
		return "", fmt.Errorf("refusing to remove volume root %s", absTarget)
	}

	return longPath(absTarget), nil
}

func removeTree(path string) (bool, error) {
	attrs, err := getFileAttributes(path)
	if err != nil {
		if isNotFound(err) {
			return false, nil
		}
		return false, fmt.Errorf("inspect %s: %w", displayPath(path), err)
	}

	if attrs&fileAttributeDirectory == 0 || attrs&fileAttributeReparsePoint != 0 {
		if err := removeLeaf(path, attrs); err != nil {
			return false, err
		}
		return true, nil
	}

	entries, err := listDir(path)
	if err != nil {
		return false, err
	}
	for _, entry := range entries {
		child := path
		if !strings.HasSuffix(child, `\`) {
			child += `\`
		}
		child += entry
		if _, err := removeTree(child); err != nil {
			return false, err
		}
	}

	if err := clearReadonly(path, attrs); err != nil {
		return false, err
	}
	if err := syscall.RemoveDirectory(mustUTF16Ptr(path)); err != nil {
		if isNotFound(err) {
			return false, nil
		}
		return false, fmt.Errorf("remove directory %s: %w", displayPath(path), err)
	}
	return true, nil
}

func removeLeaf(path string, attrs uint32) error {
	if err := clearReadonly(path, attrs); err != nil {
		return err
	}

	if attrs&fileAttributeDirectory != 0 {
		if err := syscall.RemoveDirectory(mustUTF16Ptr(path)); err != nil && !isNotFound(err) {
			return fmt.Errorf("remove directory link %s: %w", displayPath(path), err)
		}
		return nil
	}

	if err := syscall.DeleteFile(mustUTF16Ptr(path)); err != nil && !isNotFound(err) {
		return fmt.Errorf("delete file %s: %w", displayPath(path), err)
	}
	return nil
}

func listDir(path string) ([]string, error) {
	pattern := path
	if !strings.HasSuffix(pattern, `\`) {
		pattern += `\`
	}
	pattern += `*`

	var data syscall.Win32finddata
	handle, err := syscall.FindFirstFile(mustUTF16Ptr(pattern), &data)
	if err != nil {
		if isNotFound(err) {
			return nil, nil
		}
		return nil, fmt.Errorf("enumerate %s: %w", displayPath(path), err)
	}
	defer syscall.FindClose(handle)

	var entries []string
	for {
		name := syscall.UTF16ToString(data.FileName[:])
		if name != "" && name != "." && name != ".." {
			entries = append(entries, name)
		}

		err = syscall.FindNextFile(handle, &data)
		if err == nil {
			continue
		}
		if errors.Is(err, syscall.ERROR_NO_MORE_FILES) {
			break
		}
		return nil, fmt.Errorf("enumerate %s: %w", displayPath(path), err)
	}
	return entries, nil
}

func getFileAttributes(path string) (uint32, error) {
	return syscall.GetFileAttributes(mustUTF16Ptr(path))
}

func clearReadonly(path string, attrs uint32) error {
	if attrs&fileAttributeReadonly == 0 {
		return nil
	}

	nextAttrs := attrs &^ fileAttributeReadonly
	if nextAttrs == 0 {
		nextAttrs = fileAttributeNormal
	}
	if err := syscall.SetFileAttributes(mustUTF16Ptr(path), nextAttrs); err != nil && !isNotFound(err) {
		return fmt.Errorf("clear read-only attribute on %s: %w", displayPath(path), err)
	}
	return nil
}

func isNotFound(err error) bool {
	return errors.Is(err, syscall.ERROR_FILE_NOT_FOUND) ||
		errors.Is(err, syscall.ERROR_PATH_NOT_FOUND)
}

func mustUTF16Ptr(path string) *uint16 {
	ptr, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		panic(err)
	}
	return ptr
}

func mustLongPath(path string) string {
	absTarget, err := filepath.Abs(path)
	if err != nil {
		return path
	}
	return longPath(filepath.Clean(absTarget))
}

func longPath(path string) string {
	if strings.HasPrefix(path, `\\?\`) {
		return path
	}
	if strings.HasPrefix(path, `\\`) {
		return `\\?\UNC\` + strings.TrimPrefix(path, `\\`)
	}
	return `\\?\` + path
}

func displayPath(path string) string {
	if strings.HasPrefix(path, `\\?\UNC\`) {
		return `\\` + strings.TrimPrefix(path, `\\?\UNC\`)
	}
	return strings.TrimPrefix(path, `\\?\`)
}
