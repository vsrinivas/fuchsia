// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package staticanalysis

import (
	"bytes"
	"context"
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/build"
)

func TestClippyAnalyzer(t *testing.T) {
	tests := []struct {
		name          string
		path          string
		clippyTargets []build.ClippyTarget
		clippyOutputs map[string][]clippyResult
		expected      []*Finding
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
					{
						Message: "this function has too many arguments (8/7)",
						Code:    clippyCode{Code: "clippy::too_many_arguments"},
						Level:   "warning",
						Spans: []clippySpan{
							{
								FileName:    "../../src/foo.rs",
								LineStart:   1,
								LineEnd:     2,
								ColumnStart: 4,
								ColumnEnd:   5,
							},
						},
					},
				},
			},
			expected: []*Finding{
				{
					Message:   "this function has too many arguments (8/7)",
					Category:  "Clippy/warning/too_many_arguments",
					Path:      "src/foo.rs",
					StartLine: 1,
					EndLine:   2,
					// StartChar and EndChar should be decremented from the
					// one-based values produced by Clippy.
					StartChar: 3,
					EndChar:   4,
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
				var b bytes.Buffer
				for _, result := range results {
					encoded, err := json.Marshal(result)
					if err != nil {
						t.Fatal(err)
					}
					b.Write([]byte("\n"))
					b.Write(encoded)
				}
				absPath := filepath.Join(buildDir, path)
				if err := ioutil.WriteFile(absPath, b.Bytes(), 0o600); err != nil {
					t.Fatal(err)
				}
			}

			findings, err := analyzer.Analyze(context.Background(), test.path)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(test.expected, findings, cmpopts.EquateEmpty()); diff != "" {
				t.Errorf("clippy analyzer diff (-want +got): %s", diff)
			}
		})
	}
}
