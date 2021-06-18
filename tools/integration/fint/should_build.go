// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/integration/fint/filetype"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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

// skipAnalysisAllowlist returns the directories whose files cannot be
// correctly analyzed for build graph dependencies. If there are changed
// files in these directories, we should skip the analysis and always
// build. This allowlist should cover all the targets that skip strict
// source checking listed in the allowlists here:
// //build/go/BUILD.gn
// //build/rust/BUILD.gn
func skipAnalysisAllowlist() []string {
	return []string{
		"third_party/",
	}
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

	analyzeDir, err := ioutil.TempDir("", "gn-analyze")
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
	if err := runner.Run(ctx, cmd, nil, os.Stderr); err != nil {
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
		if shouldSkipAnalysis(path) {
			logger.Debugf(ctx, "Build graph analysis is not supported for file %q", path)
			return false
		}
		ft := filetype.TypeForFile(path)
		if !containsFileType(filetype.KnownFileTypes(), ft) {
			logger.Debugf(ctx, "Build graph analysis is not supported for file %q", path)
			return false
		}
	}
	return true
}

func shouldSkipAnalysis(file string) bool {
	for _, path := range skipAnalysisAllowlist() {
		if strings.HasPrefix(file, path) {
			return true
		}
	}
	return false
}

func containsFileType(collection []filetype.FileType, target filetype.FileType) bool {
	for _, ft := range collection {
		if ft == target {
			return true
		}
	}
	return false
}
