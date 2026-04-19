package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

func (s *server) executeBuildPlan(ctx context.Context, sourceDir string, req buildRequest) ([]byte, error) {
	if len(req.Steps) == 0 {
		return nil, fmt.Errorf("language=plan requires at least one build step")
	}
	if strings.TrimSpace(req.ArtifactPath) == "" {
		return nil, fmt.Errorf("language=plan requires artifact_path")
	}

	var combinedLog strings.Builder
	for index, step := range req.Steps {
		cmd, err := s.buildPlanStepCommand(ctx, sourceDir, step)
		if err != nil {
			return []byte(combinedLog.String()), fmt.Errorf("prepare build step %d/%d: %w", index+1, len(req.Steps), err)
		}

		output, err := cmd.CombinedOutput()
		if len(output) != 0 {
			fmt.Fprintf(&combinedLog, "[plan step %d/%d] %s\n", index+1, len(req.Steps), strings.Join(step.Args, " "))
			combinedLog.Write(output)
			if output[len(output)-1] != '\n' {
				combinedLog.WriteByte('\n')
			}
		}
		if err != nil {
			return []byte(combinedLog.String()), fmt.Errorf("plan step %d/%d failed: %w", index+1, len(req.Steps), err)
		}
	}

	artifactPath, err := resolveWorkspacePath(sourceDir, req.ArtifactPath)
	if err != nil {
		return []byte(combinedLog.String()), err
	}
	if _, err := os.Stat(artifactPath); err != nil {
		return []byte(combinedLog.String()), fmt.Errorf("plan artifact %q: %w", req.ArtifactPath, err)
	}

	return []byte(combinedLog.String()), nil
}

func (s *server) buildPlanStepCommand(ctx context.Context, sourceDir string, step buildStep) (*exec.Cmd, error) {
	if len(step.Args) == 0 {
		return nil, fmt.Errorf("build step must include at least argv[0]")
	}

	workingDirectory := sourceDir
	if strings.TrimSpace(step.WorkingDirectory) != "" {
		var err error
		workingDirectory, err = resolveWorkspacePath(sourceDir, step.WorkingDirectory)
		if err != nil {
			return nil, err
		}
	}

	executablePath, err := s.resolvePlanExecutable(step.Args[0])
	if err != nil {
		return nil, err
	}

	cmd := exec.CommandContext(ctx, executablePath, step.Args[1:]...)
	cmd.Args[0] = step.Args[0]
	cmd.Dir = workingDirectory
	cmd.Env = os.Environ()
	return cmd, nil
}

func (s *server) resolvePlanExecutable(raw string) (string, error) {
	name := strings.ToLower(filepath.Base(raw))
	if name == "clang" ||
		name == "clang++" ||
		name == "cleng" ||
		name == "cleng++" ||
		name == "cc" ||
		name == "c++" ||
		strings.Contains(name, "clang") ||
		strings.HasPrefix(name, "cleng") {
		if s.clengBinary == "" {
			return "", fmt.Errorf("plan step %q requires cleng to be configured", raw)
		}
		return s.clengBinary, nil
	}
	return "", fmt.Errorf("unsupported plan executable %q", raw)
}

func resolveWorkspacePath(sourceDir, raw string) (string, error) {
	clean := filepath.Clean(filepath.FromSlash(strings.TrimSpace(raw)))
	if clean == "" || clean == "." {
		return sourceDir, nil
	}
	if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") {
		return "", fmt.Errorf("path %q must stay within the uploaded workspace", raw)
	}
	absolutePath := filepath.Join(sourceDir, clean)
	if !isWithinBase(sourceDir, absolutePath) {
		return "", fmt.Errorf("path %q escapes the uploaded workspace", raw)
	}
	return absolutePath, nil
}
