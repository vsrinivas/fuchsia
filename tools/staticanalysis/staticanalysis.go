// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package staticanalysis

import (
	"context"
	"path/filepath"
)

// Analyzer is the interface that must be implemented for each linter/formatter
// that integrates with this tool.
type Analyzer interface {
	// Analyze validates a single source file, returning any findings.
	//
	// The `path` argument is relative to the checkout root.
	Analyze(ctx context.Context, path string) ([]*Finding, error)
}

// Mirrors the Tricium comment schema:
// https://chromium.googlesource.com/infra/infra/+/3b0abf2fb146af025440a48e1f7423595b1a5bfb/go/src/infra/tricium/api/v1/data.proto#122
type Finding struct {
	Category  string `json:"category"`
	Message   string `json:"message"`
	Path      string `json:"path"`
	StartLine int    `json:"start_line"`
	StartChar int    `json:"start_char"`
	EndLine   int    `json:"end_line"`
	EndChar   int    `json:"end_char"`
}

// buildPathToCheckoutPath converts a path relative to the build directory into
// a source-relative path with platform-agnostic separators.
//
// E.g. "../../src/foo/bar.py" -> "src/foo/bar.py".
func buildPathToCheckoutPath(path, buildDir, checkoutDir string) (string, error) {
	absPath := filepath.Clean(filepath.Join(buildDir, path))
	path, err := filepath.Rel(checkoutDir, absPath)
	if err != nil {
		return "", err
	}
	return filepath.ToSlash(path), nil
}
