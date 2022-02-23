// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package clippy

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

func TestAnalyzer(t *testing.T) {
	tests := []struct {
		name          string
		path          string
		clippyTargets []build.ClippyTarget
		clippyOutputs map[string][]clippyResult
		expected      []*staticanalysis.Finding
	}{
		{
			name: "file with no clippy target",
			path: "src/bar.rs",
			clippyTargets: []build.ClippyTarget{
				{
					Output:  "bar.clippy",
					Sources: []string{"../../src/foo.rs"},
				},
			},
			expected: nil,
		},
		{
			name: "file with clippy target",
			path: "src/foo.rs",
			clippyTargets: []build.ClippyTarget{
				{
					Output:  "foo.clippy",
					Sources: []string{"../../src/foo.rs"},
				},
			},
			clippyOutputs: map[string][]clippyResult{
				"foo.clippy": {
					// Results without any primary span should be ignored.
					{
						Message: "1 lint failed",
						Code:    clippyCode{Code: "clippy::summary"},
						Level:   "warning",
					},
					{
						Message: "casting integer literal to `u64` is unnecessary",
						Code:    clippyCode{Code: "clippy::unnecessary_cast"},
						Level:   "warning",
						Spans: []clippySpan{
							{
								FileName: "../../src/foo.rs",
								// Non-primary spans should be ignored.
								Primary:     false,
								LineStart:   1,
								LineEnd:     1,
								ColumnStart: 4,
								ColumnEnd:   5,
							},
							{
								FileName:    "../../src/foo.rs",
								Primary:     true,
								LineStart:   100,
								LineEnd:     100,
								ColumnStart: 14,
								ColumnEnd:   24,
							},
						},
						Children: []clippyResult{
							{
								Message: "I am a note",
								// Children with level != "help" should be ignored.
								Level: "note",
							},
							{
								Message: "try",
								Level:   "help",
								Spans: []clippySpan{
									{
										Primary:              true,
										SuggestedReplacement: "123_u64",
										FileName:             "../../src/foo.rs",
										LineStart:            5,
										LineEnd:              5,
										ColumnStart:          1,
										ColumnEnd:            11,
									},
								},
							},
							{
								// This isn't realistic, but it covers the logic
								// to ignore multiline suggested replacements.
								Message: "or try",
								Level:   "help",
								Spans: []clippySpan{
									{
										Primary:              true,
										SuggestedReplacement: "multiline\nreplacement",
										FileName:             "../../src/foo.rs",
										LineStart:            50,
										LineEnd:              51,
										ColumnStart:          7,
										ColumnEnd:            2,
									},
								},
							},
							{
								Message: "see https://clippy.example.com for docs",
								Level:   "help",
							},
						},
					},
				},
			},
			expected: []*staticanalysis.Finding{
				{
					Message: strings.Join([]string{
						"casting integer literal to `u64` is unnecessary",
						"help: try: `123_u64`",
						"help: see https://clippy.example.com for docs",
						"To reproduce locally, run `fx clippy -f src/foo.rs`",
					}, "\n\n"),
					Category:  "Clippy/warning/unnecessary_cast",
					Path:      "src/foo.rs",
					StartLine: 100,
					EndLine:   100,
					// StartChar and EndChar should be decremented from the
					// one-based values produced by Clippy.
					StartChar: 13,
					EndChar:   23,
					Suggestions: []staticanalysis.Suggestion{
						{
							Description: "try",
							Replacements: []staticanalysis.Replacement{
								{
									Replacement: "123_u64",
									Path:        "src/foo.rs",
									StartLine:   5,
									EndLine:     5,
									StartChar:   0,
									EndChar:     10,
								},
							},
						},
						{
							Description: "or try",
							Replacements: []staticanalysis.Replacement{
								{
									Replacement: "multiline\nreplacement",
									Path:        "src/foo.rs",
									StartLine:   50,
									EndLine:     51,
									StartChar:   6,
									EndChar:     1,
								},
							},
						},
					},
				},
			},
		},
		{
			name: "file with empty clippy output",
			path: "src/foo.rs",
			clippyTargets: []build.ClippyTarget{
				{
					Output:  "foo.clippy",
					Sources: []string{"../../src/foo.rs"},
				},
			},
			clippyOutputs: map[string][]clippyResult{
				"foo.clippy": nil,
			},
			expected: nil,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			checkoutDir := t.TempDir()
			buildDir := filepath.Join(checkoutDir, "out", "testing")
			if err := os.MkdirAll(buildDir, 0o700); err != nil {
				t.Fatal(err)
			}

			analyzer := ClippyAnalyzer{
				checkoutDir:   checkoutDir,
				buildDir:      buildDir,
				clippyTargets: test.clippyTargets,
			}

			for path, results := range test.clippyOutputs {
				f, err := os.Create(filepath.Join(buildDir, path))
				if err != nil {
					t.Fatal(err)
				}
				enc := json.NewEncoder(f)
				for _, result := range results {
					// Encode() automatically appends a newline, so the file will be in JSON
					// lines format.
					if err := enc.Encode(result); err != nil {
						t.Fatal(err)
					}
				}
			}

			findings, err := analyzer.Analyze(context.Background(), test.path)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(test.expected, findings, cmpopts.EquateEmpty()); diff != "" {
				t.Errorf("clippy analyzer diff (-want +got):\n%s", diff)
			}
		})
	}
}
