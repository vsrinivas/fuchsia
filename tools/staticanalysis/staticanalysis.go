// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package staticanalysis

import (
	"context"
	"fmt"
	"path"
	"path/filepath"
	"strings"
)

// Analyzer is the interface that must be implemented for each linter/formatter
// that integrates with this tool.
type Analyzer interface {
	// Analyze validates a single source file, returning any findings.
	//
	// The `path` argument is relative to the checkout root.
	Analyze(ctx context.Context, path string) ([]*Finding, error)
}

// A suggested replacement.
//
// The replacement should be for one continuous section of a file.
type Replacement struct {

	// Path to the file for this replacement.
	//
	// An empty string indicates the commit message.
	Path string `json:"path"`

	// A replacement string.
	Replacement string `json:"replacement"`

	// A continuous section of the file to replace. Required.
	StartLine int `json:"start_line"` // 1-based, inclusive.
	EndLine   int `json:"end_line"`   // 1-based, inclusive.
	StartChar int `json:"start_char"` // 0-based, inclusive.
	EndChar   int `json:"end_char"`   // 0-based, exclusive.
}

// A suggestion is associated with a single finding and contains a list of
// possible replacement texts for the text that the finding covers.
type Suggestion struct {
	Description string `json:"description"`

	Replacements []Replacement `json:"replacements"`
}

// Finding is the common schema that all linters' and formatters' outputs must
// be converted to in order to be compatible with this tool. Each Finding that
// applies to lines that are affected by the change under test may be posted as
// a Gerrit comment.
//
// Mirrors the Tricium comment schema:
// https://chromium.googlesource.com/infra/infra/+/3b0abf2fb146af025440a48e1f7423595b1a5bfb/go/src/infra/tricium/api/v1/data.proto#122
//
// TODO(olivernewman): Make this a proper proto file that gets copied into the
// recipes repository.
type Finding struct {
	// Category is the text that will be used as the header of the Gerrit
	// comment emitted for this finding.
	//
	// Should be of the form "<tool name>/<error level>/<error type>" for
	// linters, e.g. "Clippy/warning/bool_comparison", or just the name of the
	// tool for things like auto-formatters, e.g. "Gofmt".
	//
	// Required.
	Category string `json:"category"`

	// Message is a human-readable description of the finding, e.g. "variable
	// foo is not defined".
	//
	// Required.
	Message string `json:"message"`

	// Path is the path to the file within a fuchsia checkout, using forward
	// slashes as delimiters, e.g. "src/foo/bar.cc"
	//
	// If omitted, the finding will apply to the change's commit message.
	Path string `json:"path"`

	// StartLine is the starting line of the chunk of the file that the finding
	// applies to (1-indexed, inclusive).
	//
	// If omitted, the finding will apply to the entire file and a top-level
	// file comment will be emitted.
	StartLine int `json:"start_line,omitempty"`

	// EndLine is the ending line of the chunk of the file that the finding
	// applies to (1-indexed, inclusive).
	//
	// If set, must be greater than or equal to StartLine.
	//
	// If omitted, Endline is assumed to be equal to StartLine.
	EndLine int `json:"end_line,omitempty"`

	// StartChar is the index of the first character within StartLine that the
	// finding applies to (0-indexed, inclusive).
	//
	// If omitted, the finding will apply to the entire line.
	StartChar int `json:"start_char,omitempty"`

	// EndChar is the index of the last character within EndLine that the
	// finding applies to (0-indexed, exclusive).
	//
	// Required if StartChar is specified. If StartLine==EndLine, EndChar must
	// be greater than StartChar.
	EndChar int `json:"end_char,omitempty"`

	// Suggestions is a list of possible strings that could replace the text
	// highlighted by the finding.
	//
	// If set, all of StartLine, EndLine, StartChar, and EndChar must be
	// specified.
	Suggestions []Suggestion `json:"suggestions,omitempty"`
}

// Normalize makes a best effort at updating invalid/inconsistent fields to be
// sensible, returning an error if there are any field values that are invalid
// and for which a valid value cannot be determined.
//
// In an ideal world we would *validate* each analyzer finding rather than
// emitting different values from what the analyzers produce. But that would
// require potentially tedious and repetitive normalization logic in each
// analyzer, so instead we centralize the normalization logic here to make it
// easier to write new analyzers.
func (f *Finding) Normalize() error {
	if f.Category == "" {
		return fmt.Errorf("category must be set for finding: %#+v", f)
	}

	if f.Message == "" {
		return fmt.Errorf("message must be set for finding: %#+v", f)
	}

	// It's okay for Path to not be set, in which case the finding will be
	// interpreted as applying to the entire change.
	if f.Path != "" {
		if path.IsAbs(f.Path) || strings.HasPrefix(f.Path, "..") || f.Path != path.Clean(f.Path) {
			return fmt.Errorf("path must be a clean source-relative path for finding: %#+v", f)
		}
	}

	if f.StartLine > 0 {
		if f.EndLine == 0 {
			// If unset, update EndLine to be equal to StartLine.
			f.EndLine = f.StartLine
		}
		if f.StartLine == f.EndLine && f.StartChar > 0 && f.StartChar == f.EndChar {
			// Fix the common case where an analyzer emits StartChar and EndChar
			// of the same value to highlight a single-character.
			f.EndChar = f.StartChar + 1
		}

		if f.StartLine > f.EndLine || (f.StartLine == f.EndLine && f.StartChar > 0 && f.StartChar >= f.EndChar) {
			return fmt.Errorf("(start_line, start_char) must be before (end_line, end_char) for finding: %#+v", f)
		}
	} else if f.EndLine > 0 || f.StartChar > 0 || f.EndChar > 0 {
		return fmt.Errorf("start_line is unexpectedly unset for finding: %#+v", f)
	}

	if len(f.Suggestions) > 0 && (f.StartLine < 1 || f.EndLine < 1 || f.StartChar < 0 || f.EndChar <= 0) {
		return fmt.Errorf("a finding with suggestions must have a fully specified span: %#+v", f)
	}

	for _, suggestion := range f.Suggestions {
		for _, r := range suggestion.Replacements {
			if r.StartLine < 1 || r.EndLine < 1 || r.StartChar < 0 || r.EndChar <= 0 {
				return fmt.Errorf("a suggested replacement must have a fully specified span: %#+v", r)
			}
			// StartChar==EndChar is allowed for a replacement span because a
			// replacement might just insert text without replacing any
			// pre-existing text.
			if r.StartLine > r.EndLine || (r.StartLine == r.EndLine && r.StartChar > r.EndChar) {
				return fmt.Errorf("(start_line, start_char) must be before (end_line, end_char) for replacement in finding: %#+v", f)
			}
		}
	}

	return nil
}

// BuildPathToCheckoutPath converts a path relative to the build directory into
// a source-relative path with platform-agnostic separators.
//
// E.g. "../../src/foo/bar.py" -> "src/foo/bar.py".
func BuildPathToCheckoutPath(path, buildDir, checkoutDir string) (string, error) {
	absPath := filepath.Clean(filepath.Join(buildDir, path))
	path, err := filepath.Rel(checkoutDir, absPath)
	if err != nil {
		return "", err
	}
	return filepath.ToSlash(path), nil
}
