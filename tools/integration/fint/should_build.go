// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

type gnAnalyzeInput struct {
	// The files to use when determining affected targets.
	Files []string `json:"files"`

	// A list of labels for targets that are needed to run the desired tests.
	TestTargets []string `json:"test_targets"`

	// A list of labels for targets that should be rebuilt.
	AdditionalCompileTargets []string `json:"additional_compile_targets"`
}

type gnAnalyzeOutput struct {
	// The status of the analyze call.
	Status string `json:"status"`

	// The error, if present, associated with the analyze call.
	Error string `json:"error"`
}

// Valid values for the "status" field of the analyze output.
const (
	// The build graph is affected by the changed files.
	buildGraphAffectedStatus = "Found dependency"

	// The build graph is NOT affected by the changed files.
	buildGraphNotAffectedStatus = "No dependency"

	// GN can't determine whether the build graph is affected by the changed
	// files, and it conservatively considers all targets to be affected.
	unknownAffectedStatus = "Found dependency (all)"
)

// isSupportedPath returns whether the given file is in a directory whose files
// can be correctly analyzed for build graph dependencies. If the file is in an
// unsupported directory, we should skip the analysis and always build.
func isSupportedPath(path string) bool {
	// This allowlist should cover all the targets that skip strict source
	// checking listed in the allowlists here:
	// //build/go/BUILD.gn
	// //build/rust/BUILD.gn
	skipAnalysisAllowlist := []string{
		"third_party/",
	}
	for _, prefix := range skipAnalysisAllowlist {
		if strings.HasPrefix(path, prefix) {
			return false
		}
	}
	return true
}

// isSupportedFileType returns whether a given file can be correctly analyzed
// for build graph dependencies, based on its file extension.
func isSupportedFileType(path string) bool {
	if filepath.Base(path) == "OWNERS" {
		return true
	}
	supportedExtensions := []string{
		".c",
		".cc",
		".cmake",
		".cpp",
		".dart",
		".fidl",
		".gn",
		".gni",
		".go",
		".golden",
		".md",
		".pb",
		".png",
		".proto",
		".py",
		".rs",
		".rst",
		".sh",
		".template",
		".tmpl",
		".yaml",
	}
	extension := filepath.Ext(path)
	return contains(supportedExtensions, extension)
}

// shouldBuild runs `gn analyze` on the given files to determine
// whether they are part of the build graph. It returns a boolean indicating
// whether changes to those files affect the build graph, and hence whether we
// need to do a full build to test those changes.
func shouldBuild(
	ctx context.Context,
	runner subprocessRunner,
	buildDir string,
	checkoutDir string,
	platform string,
	changedFiles []string,
) (bool, error) {
	gnPath := thirdPartyPrebuilt(checkoutDir, platform, "gn")
	if !canAnalyzeFiles(ctx, changedFiles) {
		// To be safe, we should always build if we don't know how to analyze
		// all the affected files yet.
		return true, nil
	}

	input := gnAnalyzeInput{
		// Special string "all" tells GN to check all targets.
		AdditionalCompileTargets: []string{"all"},
		// TestTargets must be an empty rather than nil slice so it gets
		// serialized to an empty JSON array instead of null.
		TestTargets: []string{},
		Files:       formatFilePaths(changedFiles),
	}

	analyzeDir, err := os.MkdirTemp("", "gn-analyze")
	if err != nil {
		return false, err
	}
	defer os.RemoveAll(analyzeDir)

	inputPath := filepath.Join(analyzeDir, "input.json")
	if err := jsonutil.WriteToFile(inputPath, input); err != nil {
		return false, err
	}
	logger.Debugf(ctx, "gn analyze input: %+v", input)

	outputPath := filepath.Join(analyzeDir, "output.json")

	cmd := []string{gnPath, "analyze", buildDir, inputPath, outputPath, fmt.Sprintf("--root=%s", checkoutDir)}
	if err := runner.Run(ctx, cmd, subprocess.RunOptions{Stdout: io.Discard}); err != nil {
		return false, err
	}

	var analyzeOutput gnAnalyzeOutput
	if err := jsonutil.ReadFromFile(outputPath, &analyzeOutput); err != nil {
		return false, fmt.Errorf("failed to read gn analyze output: %w", err)
	}
	logger.Debugf(ctx, "gn analyze output: %+v", analyzeOutput)

	if analyzeOutput.Error != "" {
		return false, fmt.Errorf("gn analyze error: %q", analyzeOutput.Error)
	}

	switch analyzeOutput.Status {
	case buildGraphAffectedStatus, unknownAffectedStatus:
		return true, nil
	case buildGraphNotAffectedStatus:
		return false, nil
	default:
		return false, fmt.Errorf("gn analyze produced unrecognized status: %s", analyzeOutput.Status)
	}
}

// formatFilePaths converts a series of file paths relative to the checkout root
// into the format expected by `gn analyze`.
func formatFilePaths(paths []string) []string {
	formatted := []string{} // Empty, not nil, so it serializes to [].
	for _, path := range paths {
		formatted = append(formatted, "//"+path)
	}
	return formatted
}

func canAnalyzeFiles(ctx context.Context, changedFiles []string) bool {
	for _, path := range changedFiles {
		if !isSupportedPath(path) || !isSupportedFileType(path) {
			logger.Debugf(ctx, "Build graph analysis is not supported for file %q", path)
			return false
		}
	}
	return true
}
