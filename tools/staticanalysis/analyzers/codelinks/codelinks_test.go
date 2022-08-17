// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codelinks

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

func TestAnalyzer(t *testing.T) {
	path := "foo.cc"

	tests := []struct {
		name     string
		lines    []string
		expected []*staticanalysis.Finding
	}{
		{
			name: "no bad links",
			lines: []string{
				"int main() {",
				"  return 0",
				"}",
			},
			expected: nil,
		},
		{
			name: "file with two bad links",
			lines: []string{
				"// See fuchsia.googlesource.com/fuchsia/+/HEAD/docs/main_function.md",
				"int main() {",
				"  // See https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/docs/c++/returns.md",
				"  return 0",
				"}",
			},
			expected: []*staticanalysis.Finding{
				{
					Category:  "BadCodeLink",
					Message:   "Documentation links should point to fuchsia.dev rather than fuchsia.googlesource.com.",
					Path:      path,
					StartLine: 1,
					EndLine:   1,
					StartChar: 7,
					EndChar:   68,
					Suggestions: []staticanalysis.Suggestion{
						{
							Replacements: []staticanalysis.Replacement{
								{
									Path:        path,
									Replacement: "https://fuchsia.dev/fuchsia-src/main_function",
									StartLine:   1,
									EndLine:     1,
									StartChar:   7,
									EndChar:     68,
								},
							},
						},
					},
				},
				{
					Category:  "BadCodeLink",
					Message:   "Documentation links should point to fuchsia.dev rather than fuchsia.googlesource.com.",
					Path:      path,
					StartLine: 3,
					EndLine:   3,
					StartChar: 9,
					EndChar:   87,
					Suggestions: []staticanalysis.Suggestion{
						{
							Replacements: []staticanalysis.Replacement{
								{
									Path:        path,
									Replacement: "https://fuchsia.dev/fuchsia-src/c++/returns",
									StartLine:   3,
									EndLine:     3,
									StartChar:   9,
									EndChar:     87,
								},
							},
						},
					},
				},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			checkoutDir := t.TempDir()
			analyzer := analyzer{checkoutDir: checkoutDir}

			contents := []byte(strings.Join(test.lines, "\n") + "\n")
			if err := os.WriteFile(filepath.Join(checkoutDir, path), contents, 0o600); err != nil {
				t.Fatal(err)
			}

			findings, err := analyzer.Analyze(context.Background(), path)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(test.expected, findings, cmpopts.EquateEmpty()); diff != "" {
				t.Errorf("Analyzer diff (-want +got):\n%s", diff)
			}
		})
	}
}
