package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

type cmakeBuildOptions struct {
	serverURL     string
	sourcePath    string
	buildDir      string
	outputPath    string
	target        string
	goos          string
	goarch        string
	timeout       time.Duration
	generator     string
	buildType     string
	toolchainFile string
	cmakeArgs     []string
}

func parseCMakeBuildOptions(serverURL string, args []string) (cmakeBuildOptions, error) {
	opts := cmakeBuildOptions{
		serverURL:  serverURL,
		sourcePath: ".",
		goos:       "windows",
		goarch:     "amd64",
		timeout:    20 * time.Minute,
		generator:  "Ninja",
		buildType:  "Release",
	}

	var sourceSet bool
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "-h", "--help":
			printUsage()
			return opts, errUsageRequested
		case "-target":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -target")
			}
			opts.target = args[i]
		case "-build-dir":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -build-dir")
			}
			opts.buildDir = args[i]
		case "-toolchain":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -toolchain")
			}
			opts.toolchainFile = args[i]
		case "-o", "--output":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -o")
			}
			opts.outputPath = args[i]
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
		case "-generator":
			i++
			if i >= len(args) {
				return opts, errors.New("missing value for -generator")
			}
			opts.generator = args[i]
		case "-config", "-build-type":
			i++
			if i >= len(args) {
				return opts, fmt.Errorf("missing value for %s", arg)
			}
			opts.buildType = args[i]
		case "--":
			opts.cmakeArgs = append(opts.cmakeArgs, args[i+1:]...)
			i = len(args)
		default:
			if strings.HasPrefix(arg, "-") {
				opts.cmakeArgs = append(opts.cmakeArgs, arg)
				if compilerArgConsumesNextValue(arg) {
					i++
					if i >= len(args) {
						return opts, fmt.Errorf("missing value for CMake flag %q", arg)
					}
					opts.cmakeArgs = append(opts.cmakeArgs, args[i])
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
	if strings.TrimSpace(opts.target) == "" {
		return opts, errors.New("cmake-build requires -target <cmake-target>")
	}
	if !strings.EqualFold(strings.TrimSpace(opts.generator), "Ninja") {
		return opts, fmt.Errorf("cmake-build currently requires -generator Ninja, got %q", opts.generator)
	}
	return opts, nil
}

func runCMakeBuild(opts cmakeBuildOptions) error {
	sourceRoot, err := filepath.Abs(opts.sourcePath)
	if err != nil {
		return fmt.Errorf("resolve source path: %w", err)
	}

	info, err := os.Stat(sourceRoot)
	if err != nil {
		return fmt.Errorf("stat %s: %w", sourceRoot, err)
	}
	if !info.IsDir() {
		return fmt.Errorf("cmake-build source path must be a directory, got %s", sourceRoot)
	}

	buildDir, err := resolveCMakeBuildDir(sourceRoot, opts.buildDir, opts.target)
	if err != nil {
		return err
	}
	if !isWithinBase(sourceRoot, buildDir) {
		return fmt.Errorf("build directory %s must stay inside the source root so cliant can archive generated files", buildDir)
	}

	toolchainFile, err := resolveCMakeToolchainFile(sourceRoot, opts.toolchainFile)
	if err != nil {
		return err
	}

	if err := runLocalCMakeConfigure(sourceRoot, buildDir, toolchainFile, opts); err != nil {
		return err
	}

	steps, artifactPath, err := exportNinjaBuildPlan(sourceRoot, buildDir, opts.target)
	if err != nil {
		return err
	}
	if artifactPath == "" {
		return fmt.Errorf("could not determine final artifact path for target %s", opts.target)
	}

	outputPath := opts.outputPath
	if outputPath == "" {
		outputPath = filepath.Base(filepath.FromSlash(artifactPath))
	}

	archivePath, err := createSourceArchive(sourceRoot)
	if err != nil {
		return err
	}
	defer os.Remove(archivePath)

	return submitArchiveBuild(opts.serverURL, archivePath, buildRequest{
		Target:       opts.target,
		OutputName:   filepath.Base(outputPath),
		GOOS:         opts.goos,
		GOARCH:       opts.goarch,
		CGOEnabled:   "1",
		Language:     "plan",
		Steps:        steps,
		ArtifactPath: artifactPath,
	}, outputPath, opts.timeout)
}

func resolveCMakeBuildDir(sourceRoot, rawBuildDir, target string) (string, error) {
	if strings.TrimSpace(rawBuildDir) == "" {
		return filepath.Join(sourceRoot, ".cliant-cmake", sanitizePathToken(target)), nil
	}
	if filepath.IsAbs(rawBuildDir) {
		return filepath.Clean(rawBuildDir), nil
	}
	return filepath.Join(sourceRoot, filepath.Clean(filepath.FromSlash(rawBuildDir))), nil
}

func resolveCMakeToolchainFile(sourceRoot, rawToolchain string) (string, error) {
	candidates := make([]string, 0, 3)
	if strings.TrimSpace(rawToolchain) != "" {
		if filepath.IsAbs(rawToolchain) {
			candidates = append(candidates, filepath.Clean(rawToolchain))
		} else {
			candidates = append(candidates, filepath.Join(sourceRoot, filepath.Clean(filepath.FromSlash(rawToolchain))))
		}
	} else {
		candidates = append(candidates,
			filepath.Join(sourceRoot, "cmake", "xbax-remote-windows-toolchain.cmake"),
			filepath.Join(sourceRoot, "xbax-remote-windows-toolchain.cmake"),
		)
	}

	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, nil
		}
	}

	if strings.TrimSpace(rawToolchain) != "" {
		return "", fmt.Errorf("toolchain file %q does not exist", rawToolchain)
	}
	return "", errors.New("no toolchain file found; pass -toolchain or add cmake/xbax-remote-windows-toolchain.cmake")
}

func runLocalCMakeConfigure(sourceRoot, buildDir, toolchainFile string, opts cmakeBuildOptions) error {
	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		return fmt.Errorf("create build directory: %w", err)
	}

	args := []string{
		"-S", sourceRoot,
		"-B", buildDir,
		"-G", "Ninja",
		"-DCMAKE_BUILD_TYPE=" + opts.buildType,
		"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
		"-DCMAKE_TOOLCHAIN_FILE=" + toolchainFile,
	}
	args = append(args, opts.cmakeArgs...)

	cmd := exec.Command("cmake", args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("local cmake configure failed: %v\n%s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func exportNinjaBuildPlan(sourceRoot, buildDir, target string) ([]buildStep, string, error) {
	cmd := exec.Command("ninja", "-C", buildDir, "-t", "commands", target)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, "", fmt.Errorf("export ninja commands for %s: %v\n%s", target, err, strings.TrimSpace(string(output)))
	}

	var (
		steps         []buildStep
		finalArtifact string
	)
	scanner := bufio.NewScanner(strings.NewReader(string(output)))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		workingDirectory, argv, err := parseNinjaCommandLine(buildDir, line)
		if err != nil {
			return nil, "", fmt.Errorf("parse ninja command %q: %w", line, err)
		}
		if len(argv) == 0 {
			continue
		}
		if !isCompilerDriverCommand(argv[0]) {
			return nil, "", fmt.Errorf("unsupported build step %q; cmake-build currently only replays compiler/linker commands", argv[0])
		}

		step, outputRel, err := rewriteBuildStepForRemote(sourceRoot, workingDirectory, argv)
		if err != nil {
			return nil, "", err
		}
		steps = append(steps, step)
		if outputRel != "" && !isCompileOnlyDriverInvocation(step.Args) {
			finalArtifact = outputRel
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, "", fmt.Errorf("scan ninja command stream: %w", err)
	}
	if len(steps) == 0 {
		return nil, "", fmt.Errorf("ninja emitted no compiler commands for target %s", target)
	}
	return steps, filepath.ToSlash(finalArtifact), nil
}

func parseNinjaCommandLine(defaultWorkingDirectory, line string) (string, []string, error) {
	workingDirectory := defaultWorkingDirectory
	commandText := strings.TrimSpace(line)

	if idx := strings.Index(commandText, " && "); idx >= 0 {
		prefix := strings.TrimSpace(commandText[:idx])
		fields, err := splitPOSIXFields(prefix)
		if err == nil && len(fields) == 2 && fields[0] == "cd" {
			workingDirectory = fields[1]
			commandText = strings.TrimSpace(commandText[idx+4:])
		}
	}
	commandText = strings.TrimSpace(strings.TrimPrefix(commandText, ": && "))
	commandText = strings.TrimSpace(strings.TrimSuffix(commandText, " && :"))

	argv, err := splitPOSIXFields(commandText)
	if err != nil {
		return "", nil, err
	}
	return workingDirectory, argv, nil
}

func splitPOSIXFields(input string) ([]string, error) {
	var (
		fields   []string
		current  strings.Builder
		inSingle bool
		inDouble bool
		escaped  bool
	)

	flush := func() {
		if current.Len() == 0 {
			return
		}
		fields = append(fields, current.String())
		current.Reset()
	}

	for _, ch := range input {
		switch {
		case escaped:
			current.WriteRune(ch)
			escaped = false
		case ch == '\\' && !inSingle:
			escaped = true
		case ch == '\'' && !inDouble:
			inSingle = !inSingle
		case ch == '"' && !inSingle:
			inDouble = !inDouble
		case (ch == ' ' || ch == '\t') && !inSingle && !inDouble:
			flush()
		default:
			current.WriteRune(ch)
		}
	}

	if escaped || inSingle || inDouble {
		return nil, fmt.Errorf("unterminated shell quoting in %q", input)
	}
	flush()
	return fields, nil
}

func rewriteBuildStepForRemote(sourceRoot, workingDirectory string, argv []string) (buildStep, string, error) {
	workingDirectory = filepath.Clean(workingDirectory)
	if !filepath.IsAbs(workingDirectory) {
		workingDirectory = filepath.Join(sourceRoot, workingDirectory)
	}
	if !isWithinBase(sourceRoot, workingDirectory) {
		return buildStep{}, "", fmt.Errorf("working directory %s escapes the source root", workingDirectory)
	}

	stepArgs := make([]string, 0, len(argv))
	stepArgs = append(stepArgs, filepath.Base(argv[0]))

	var outputRel string
	for i := 1; i < len(argv); i++ {
		arg := argv[i]
		if consumesPathValue(arg) {
			if i+1 >= len(argv) {
				return buildStep{}, "", fmt.Errorf("missing value for %s", arg)
			}
			rewrittenValue, absoluteValue, err := rewriteCommandPath(sourceRoot, workingDirectory, argv[i+1], arg)
			if err != nil {
				return buildStep{}, "", err
			}
			stepArgs = append(stepArgs, arg, rewrittenValue)
			if arg == "-o" {
				outputRel, err = relativeToRoot(sourceRoot, absoluteValue)
				if err != nil {
					return buildStep{}, "", err
				}
			}
			i++
			continue
		}

		if prefix, value, ok := splitAttachedPathFlag(arg); ok {
			rewrittenValue, absoluteValue, err := rewriteCommandPath(sourceRoot, workingDirectory, value, prefix)
			if err != nil {
				return buildStep{}, "", err
			}
			stepArgs = append(stepArgs, prefix+rewrittenValue)
			if prefix == "-o" {
				outputRel, err = relativeToRoot(sourceRoot, absoluteValue)
				if err != nil {
					return buildStep{}, "", err
				}
			}
			continue
		}

		if strings.HasPrefix(arg, "-Wl,") {
			rewrittenArg, err := rewriteLinkerArgument(sourceRoot, workingDirectory, arg)
			if err != nil {
				return buildStep{}, "", err
			}
			stepArgs = append(stepArgs, rewrittenArg)
			continue
		}

		rewrittenArg, err := rewritePositionalArgument(sourceRoot, workingDirectory, arg)
		if err != nil {
			return buildStep{}, "", err
		}
		stepArgs = append(stepArgs, rewrittenArg)
	}

	relWorkingDirectory, err := relativeToRoot(sourceRoot, workingDirectory)
	if err != nil {
		return buildStep{}, "", err
	}
	if relWorkingDirectory == "." {
		relWorkingDirectory = ""
	}

	return buildStep{
		Args:             stepArgs,
		WorkingDirectory: filepath.ToSlash(relWorkingDirectory),
	}, filepath.ToSlash(outputRel), nil
}

func consumesPathValue(arg string) bool {
	switch arg {
	case "-o", "-MF", "-I", "-L", "-include", "-imacros", "-idirafter", "-iframework", "-iquote", "-isystem", "-isysroot", "-syslibroot":
		return true
	default:
		return false
	}
}

func splitAttachedPathFlag(arg string) (string, string, bool) {
	for _, prefix := range []string{"-I", "-L", "-o", "-MF", "-include", "-imacros", "-idirafter", "-iframework", "-iquote", "-isystem", "-isysroot", "-syslibroot"} {
		if strings.HasPrefix(arg, prefix) && len(arg) > len(prefix) {
			return prefix, arg[len(prefix):], true
		}
	}
	return "", "", false
}

func rewriteLinkerArgument(sourceRoot, workingDirectory, arg string) (string, error) {
	parts := strings.Split(arg, ",")
	expectPath := false

	for i := 1; i < len(parts); i++ {
		part := parts[i]
		if expectPath {
			rewritten, _, err := rewriteCommandPath(sourceRoot, workingDirectory, part, "-Wl")
			if err != nil {
				return "", err
			}
			parts[i] = rewritten
			expectPath = false
			continue
		}

		if option, value, found := strings.Cut(part, "="); found {
			if linkerOptionTakesPath(option) {
				rewritten, _, err := rewriteCommandPath(sourceRoot, workingDirectory, value, "-Wl")
				if err != nil {
					return "", err
				}
				parts[i] = option + "=" + rewritten
			}
			continue
		}

		if linkerOptionTakesPath(part) {
			expectPath = true
		}
	}

	return strings.Join(parts, ","), nil
}

func linkerOptionTakesPath(option string) bool {
	switch option {
	case "--out-implib", "--output-def", "-Map", "-T":
		return true
	default:
		return false
	}
}

func rewritePositionalArgument(sourceRoot, workingDirectory, arg string) (string, error) {
	if strings.HasPrefix(arg, "-") && !strings.HasPrefix(arg, "@") {
		return arg, nil
	}
	if strings.HasPrefix(arg, "@") {
		rewritten, _, err := rewriteCommandPath(sourceRoot, workingDirectory, strings.TrimPrefix(arg, "@"), "@")
		if err != nil {
			return "", err
		}
		return "@" + rewritten, nil
	}
	if !filepath.IsAbs(arg) {
		return filepath.ToSlash(arg), nil
	}
	rewritten, _, err := rewriteCommandPath(sourceRoot, workingDirectory, arg, "input")
	return rewritten, err
}

func rewriteCommandPath(sourceRoot, workingDirectory, rawValue, flag string) (string, string, error) {
	if rawValue == "" {
		return rawValue, rawValue, nil
	}

	var absolutePath string
	if filepath.IsAbs(rawValue) {
		absolutePath = filepath.Clean(rawValue)
	} else {
		absolutePath = filepath.Clean(filepath.Join(workingDirectory, filepath.FromSlash(rawValue)))
	}

	if !isWithinBase(sourceRoot, absolutePath) {
		return "", "", fmt.Errorf("%s path %q is outside the uploaded source root", flag, rawValue)
	}

	rewritten, err := filepath.Rel(workingDirectory, absolutePath)
	if err != nil {
		return "", "", fmt.Errorf("rewrite path %q: %w", rawValue, err)
	}
	return filepath.ToSlash(rewritten), absolutePath, nil
}

func relativeToRoot(sourceRoot, absolutePath string) (string, error) {
	if !isWithinBase(sourceRoot, absolutePath) {
		return "", fmt.Errorf("path %s is outside the uploaded source root", absolutePath)
	}
	rel, err := filepath.Rel(sourceRoot, absolutePath)
	if err != nil {
		return "", fmt.Errorf("resolve relative path for %s: %w", absolutePath, err)
	}
	return rel, nil
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

func isCompilerDriverCommand(raw string) bool {
	name := strings.ToLower(filepath.Base(raw))
	return name == "clang" ||
		name == "clang++" ||
		name == "cc" ||
		name == "c++" ||
		strings.Contains(name, "clang")
}

func isCompileOnlyDriverInvocation(argv []string) bool {
	for _, arg := range argv[1:] {
		switch arg {
		case "-c", "-E", "-S", "-fsyntax-only", "-emit-ast":
			return true
		}
	}
	return false
}

func sanitizePathToken(value string) string {
	value = strings.TrimSpace(value)
	if value == "" {
		return "remote"
	}
	replacer := strings.NewReplacer(
		"/", "-",
		"\\", "-",
		":", "-",
		" ", "-",
	)
	value = replacer.Replace(value)
	for strings.Contains(value, "--") {
		value = strings.ReplaceAll(value, "--", "-")
	}
	value = strings.Trim(value, "-.")
	if value == "" {
		return "remote"
	}
	return value
}

func submitArchiveBuild(serverURL, archivePath string, request buildRequest, outputPath string, timeout time.Duration) error {
	requestBody, contentType, err := makeMultipartBody(archivePath, request)
	if err != nil {
		return err
	}

	client := &http.Client{Timeout: timeout}
	httpRequest, err := http.NewRequest(http.MethodPost, serverURL+"/build", requestBody)
	if err != nil {
		return fmt.Errorf("create build request: %w", err)
	}
	httpRequest.Header.Set("Content-Type", contentType)

	response, err := client.Do(httpRequest)
	if err != nil {
		return fmt.Errorf("request build: %w", err)
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusOK {
		return decodeServerError(response)
	}

	if err := os.MkdirAll(filepath.Dir(outputPath), 0o755); err != nil {
		return fmt.Errorf("create output directory: %w", err)
	}

	output, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("create %s: %w", outputPath, err)
	}
	defer output.Close()

	written, err := io.Copy(output, response.Body)
	if err != nil {
		return fmt.Errorf("save build output: %w", err)
	}

	fmt.Printf("saved %s (%d bytes)\n", outputPath, written)
	return nil
}
