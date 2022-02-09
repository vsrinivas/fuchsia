// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package staticanalysis

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// ClippyAnalyzer implements the Analyzer interface for Clippy, a Rust linter.
//
// Clippy runs within the build system because it needs access to each Rust
// library's full dependency tree, and it outputs files within the build
// directory containing Clippy findings, so this checker reads findings from
// those files (and assumes they've already been built) rather than running
// Clippy itself.
type ClippyAnalyzer struct {
	buildDir      string
	checkoutDir   string
	pythonPath    string
	clippyTargets []build.ClippyTarget
}

var _ Analyzer = &ClippyAnalyzer{}

func NewClippyAnalyzer(checkoutDir string, modules *build.Modules) (*ClippyAnalyzer, error) {
	return &ClippyAnalyzer{
		buildDir:      modules.BuildDir(),
		checkoutDir:   checkoutDir,
		clippyTargets: modules.ClippyTargets(),
	}, nil
}

func (c *ClippyAnalyzer) Analyze(ctx context.Context, path string) ([]*Finding, error) {
	buildRelPath, err := filepath.Rel(c.buildDir, filepath.Join(c.checkoutDir, path))
	if err != nil {
		return nil, err
	}
	clippyTarget, hasClippy := c.clippyTargetForFile(buildRelPath)
	if !hasClippy {
		return nil, nil
	}

	// Make sure the Clippy output file was built.
	outputPath := filepath.Join(c.buildDir, clippyTarget.Output)
	if _, err := os.Stat(outputPath); errors.Is(err, os.ErrNotExist) {
		// TODO(olivernewman): consider making these failures blocking once
		// we're confident that the configuration is correct and the files
		// always exist when they should.
		logger.Warningf(ctx, "clippy output file %s for source file %s does not exist", clippyTarget.Output, path)
		return nil, nil
	}

	contents, err := ioutil.ReadFile(outputPath)
	if err != nil {
		return nil, err
	}
	contents = bytes.TrimSpace(contents)
	// Consider an empty file to have no Clippy findings.
	if len(contents) == 0 {
		return nil, nil
	}

	var findings []*Finding
	// The clippy file is in JSON lines format.
	for _, line := range bytes.Split(contents, []byte("\n")) {
		var result clippyResult
		if err := json.Unmarshal(line, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal clippy output line (%q): %w", line, err)
		}
		// Note that some clippy findings are just summaries of all the other
		// clippy findings and don't contain any spans. That's fine, we'll just
		// skip them.
		for _, span := range result.Spans {
			spanPath, err := buildPathToCheckoutPath(span.FileName, c.buildDir, c.checkoutDir)
			if err != nil {
				return nil, err
			}
			// Each Clippy output file contains the findings from an entire Rust
			// library that may contain many files, so skip findings from any
			// file besides the one we're currently checking.
			if spanPath != path {
				continue
			}
			category := fmt.Sprintf(
				"Clippy/%s/%s",
				result.Level,
				strings.TrimPrefix(result.Code.Code, "clippy::"))
			findings = append(findings, &Finding{
				Category:  category,
				Message:   result.Message,
				Path:      spanPath,
				StartLine: span.LineStart,
				EndLine:   span.LineEnd,
				StartChar: span.ColumnStart,
				EndChar:   span.ColumnEnd,
			})
		}
	}
	return findings, nil
}

// clippyTargetForFile returns the Clippy output target for the library that a
// given source file is included in. If the file is not associated with any
// Clippy target, returns false.
func (c *ClippyAnalyzer) clippyTargetForFile(buildRelPath string) (target build.ClippyTarget, ok bool) {
	for _, target := range c.clippyTargets {
		for _, source := range target.Sources {
			// Assumes each file only feeds into a single clippy target.
			if source == buildRelPath {
				return target, true
			}
		}
	}
	return build.ClippyTarget{}, false
}

// clippyResult represents one clippy finding.
type clippyResult struct {
	// Message is the human-readable explanation of the finding.
	Message string     `json:"message"`
	Code    clippyCode `json:"code"`
	// Level is the severity of the finding, e.g. "warning".
	Level string `json:"level"`
	// Spans is the file ranges that the finding applies to.
	Spans []clippySpan `json:"spans"`
}

type clippyCode struct {
	// Code is the codename for this result type plus a "clippy::" prefix, e.g.
	// "clippy::too_many_arguments".
	Code string `json:"code"`
}

type clippySpan struct {
	// Path relative to the build dir.
	FileName string `json:"file_name"`

	LineStart int `json:"line_start"`
	LineEnd   int `json:"line_end"`

	ColumnStart int `json:"column_start"`
	ColumnEnd   int `json:"column_end"`
}
