// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package clippy

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
	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

// ClippyAnalyzer implements the ClippyAnalyzer interface for Clippy, a Rust linter.
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

var _ staticanalysis.Analyzer = &ClippyAnalyzer{}

func New(checkoutDir string, modules *build.Modules) (*ClippyAnalyzer, error) {
	return &ClippyAnalyzer{
		buildDir:      modules.BuildDir(),
		checkoutDir:   checkoutDir,
		clippyTargets: modules.ClippyTargets(),
	}, nil
}

func (c *ClippyAnalyzer) Analyze(ctx context.Context, path string) ([]*staticanalysis.Finding, error) {
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

	var findings []*staticanalysis.Finding
	// The clippy file is in JSON lines format.
	for _, line := range bytes.Split(contents, []byte("\n")) {
		var result clippyResult
		if err := json.Unmarshal(line, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal clippy output line (%q): %w", line, err)
		}

		primarySpan, ok := result.primarySpan(ctx)
		if !ok {
			// Skip any result that doesn't have any primary span. This will be
			// the case for e.g. results that have no spans because they just
			// summarize other results from the same library.
			continue
		}

		spanPath, err := staticanalysis.BuildPathToCheckoutPath(primarySpan.FileName, c.buildDir, c.checkoutDir)
		if err != nil {
			return nil, err
		}
		// Each Clippy output file contains the findings from an entire Rust
		// library that may contain many files, so skip findings from any
		// file besides the one we're currently checking.
		if spanPath != path {
			continue
		}

		var suggestions []staticanalysis.Suggestion

		messageLines := []string{result.Message}

		// Clippy output often contains "help" messages providing suggestions
		// for fixes and links to documentation. Append these messages to the
		// finding's text.
		for _, child := range result.Children {
			if child.Level != "help" {
				continue
			}
			msg := child.Message
			if span, ok := child.primarySpan(ctx); ok && span.SuggestedReplacement != "" {
				replacementPath, err := staticanalysis.BuildPathToCheckoutPath(span.FileName, c.buildDir, c.checkoutDir)
				if err != nil {
					return nil, err
				}
				if replacementPath != path {
					continue
				}
				suggestions = append(suggestions, staticanalysis.Suggestion{
					Description: child.Message,
					Replacements: []staticanalysis.Replacement{
						{
							Path:        replacementPath,
							Replacement: span.SuggestedReplacement,
							StartLine:   span.LineStart,
							EndLine:     span.LineEnd,
							// Clippy uses one-based column numbers but
							// staticanalysis finding character indices must be
							// zero-based.
							StartChar: span.ColumnStart - 1,
							EndChar:   span.ColumnEnd - 1,
						},
					},
				})
				if strings.Contains(span.SuggestedReplacement, "\n") {
					// If the suggested replacement is multiple lines, it's
					// likely that it starts in the middle of one line and won't
					// make much sense when rendered out of context.
					continue
				}
				// If there's a suggested replacement, child.Message will be
				// some text like "try" that is intended to be followed by the
				// suggested replacement.
				msg = fmt.Sprintf("%s: `%s`", child.Message, span.SuggestedReplacement)
			}
			messageLines = append(messageLines, fmt.Sprintf("help: %s", msg))
		}

		messageLines = append(messageLines, fmt.Sprintf("To reproduce locally, run `fx clippy -f %s`", path))

		lintID := strings.TrimPrefix(result.Code.Code, "clippy::")
		category := fmt.Sprintf("Clippy/%s/%s", result.Level, lintID)

		findings = append(findings, &staticanalysis.Finding{
			Category:  category,
			Message:   strings.Join(messageLines, "\n\n"),
			Path:      path,
			StartLine: primarySpan.LineStart,
			EndLine:   primarySpan.LineEnd,
			// Clippy uses one-based column numbers but staticanalysis finding
			// character indices must be zero-based.
			StartChar:   primarySpan.ColumnStart - 1,
			EndChar:     primarySpan.ColumnEnd - 1,
			Suggestions: suggestions,
		})
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
	// Children lists auxiliary information that may be helpful for
	// understanding the lint result (help messages, references to related code,
	// etc.).
	Children []clippyResult `json:"children"`
}

// primarySpan returns the first (presumed only) primary span, if any, of this
// clippy result. It returns false if no primary span was found.
func (cr *clippyResult) primarySpan(ctx context.Context) (clippySpan, bool) {
	var primarySpans []clippySpan
	for _, s := range cr.Spans {
		if s.Primary {
			primarySpans = append(primarySpans, s)
		}
	}
	if len(primarySpans) == 0 {
		return clippySpan{}, false
	}
	if len(primarySpans) > 1 {
		// Note that some Clippy lints such as `uninit_vec` actually do have
		// multiple primary spans, but we'll always use the first one.
		logger.Warningf(ctx, "Multiple primary spans found in clippy result: %+v", cr)
	}
	return primarySpans[0], true
}

type clippyCode struct {
	// Code is the codename for this result type plus a "clippy::" prefix, e.g.
	// "clippy::too_many_arguments".
	Code string `json:"code"`
}

type clippySpan struct {
	// Path relative to the build dir.
	FileName string `json:"file_name"`

	// Whether this is the primary span for the associated Clippy result.
	Primary bool `json:"is_primary"`

	// Suggested replacement text to fix the lint (optional).
	SuggestedReplacement string `json:"suggested_replacement"`

	LineStart int `json:"line_start"`
	LineEnd   int `json:"line_end"`

	ColumnStart int `json:"column_start"`
	ColumnEnd   int `json:"column_end"`
}
