// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rfcmeta

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

type file struct {
	path    string
	content string
}

func TestAnalyzer(t *testing.T) {
	tests := []struct {
		name     string
		path     string
		files    []file
		expected []*staticanalysis.Finding
	}{
		{
			name: "toc won't parse",
			path: "docs/contribute/governance/rfcs/_toc.yaml",
			files: []file{{
				path:    "docs/contribute/governance/rfcs/_toc.yaml",
				content: "asd'f",
			}},
			expected: []*staticanalysis.Finding{
				{
					Category: "rfcmeta/toc/failed_to_parse",
					Message:  "Failed to parse yaml: yaml: unmarshal errors:\n  line 1: cannot unmarshal !!str `asd'f` into rfcmeta.toc",
					Path:     "docs/contribute/governance/rfcs/_toc.yaml",
				},
			},
		},
		{
			name: "rfc index won't parse",
			path: "docs/contribute/governance/rfcs/_rfcs.yaml",
			files: []file{{
				path:    "docs/contribute/governance/rfcs/_rfcs.yaml",
				content: "asd'f",
			}},
			expected: []*staticanalysis.Finding{
				{
					Category: "rfcmeta/index/failed_to_parse",
					Message:  "Failed to parse yaml: yaml: unmarshal errors:\n  line 1: cannot unmarshal !!str `asd'f` into []*rfcmeta.rfcIndexEntry",
					Path:     "docs/contribute/governance/rfcs/_rfcs.yaml",
				},
			},
		},
		{
			name: "rfc index won't parse, rfc file still works",
			path: "docs/contribute/governance/rfcs/1234_my_rfc.md",
			files: []file{{
				path:    "docs/contribute/governance/rfcs/_rfcs.yaml",
				content: "asd'f",
			},
				{
					path: "docs/contribute/governance/rfcs/1234_my_rfc.md",
					content: `
<!-- mdformat off(templates not supported) -->
{% set rfcid = "RFC-1234" %}
`,
				}},
			expected: []*staticanalysis.Finding{
				{
					Category: "rfcmeta/file/not_in_toc",
					Message:  "No matching entry in _toc.yaml",
					Path:     "docs/contribute/governance/rfcs/1234_my_rfc.md",
				},
				{
					Category: "rfcmeta/file/not_in_index",
					Message:  "RFC is not listed in _rfcs.yaml",
					Path:     "docs/contribute/governance/rfcs/1234_my_rfc.md",
				},
			},
		},
		{
			name: "toc path mismatch",
			path: "docs/contribute/governance/rfcs/_toc.yaml",
			files: []file{
				{
					path: "docs/contribute/governance/rfcs/_toc.yaml",
					content: `
toc:
- title: "RFC-1234"
  path: "docs/contribute/governance/rfcs/0001_rfc_process.md"
`,
				},
				{
					path: "docs/contribute/governance/rfcs/1234_my_rfc.md",
					content: `
<!-- mdformat off(templates not supported) -->
{% set rfcid = "RFC-1234" %}
`,
				}},
			expected: []*staticanalysis.Finding{
				{
					Category:  "rfcmeta/toc/unexpected_path",
					Message:   `path for "RFC-1234" should begin with "/docs/contribute/governance/rfcs/1234_"; found "docs/contribute/governance/rfcs/0001_rfc_process.md"`,
					Path:      "docs/contribute/governance/rfcs/_toc.yaml",
					StartLine: 3,
					EndLine:   3,
				},
				{
					Category:  "rfcmeta/toc/file_not_found",
					Message:   `File "docs/contribute/governance/rfcs/0001_rfc_process.md" doesn't exist`,
					Path:      "docs/contribute/governance/rfcs/_toc.yaml",
					StartLine: 3,
					EndLine:   3,
				},
			},
		},
		{
			name: "rfc in toc and index",
			path: "docs/contribute/governance/rfcs/1234_my_rfc.md",
			files: []file{
				{
					path: "docs/contribute/governance/rfcs/_toc.yaml",
					content: `
toc:
- title: "RFCs"
  section:
  - title: "RFC-1234"
    path: "/docs/contribute/governance/rfcs/1234_my_rfc.md"
`,
				},
				{
					path: "docs/contribute/governance/rfcs/_rfcs.yaml",
					content: `
- name: 'RFC-1234'
  title: 'My RFC'
  short_description: "This RFC is mine. You can't have it."
  authors: ['page@google.com']
  file: '1234_my_rfc.md'
`,
				},
				{
					path:    "docs/contribute/governance/rfcs/1234_my_rfc.md",
					content: `{% set rfcid = "RFC-1234" %}`,
				},
			},
			expected: nil,
		},
		{
			name: "rfcid tag missing ",
			path: "docs/contribute/governance/rfcs/1234_my_rfc.md",
			files: []file{
				{
					path: "docs/contribute/governance/rfcs/_toc.yaml",
					content: `
toc:
- title: "RFCs"
  section:
  - title: "RFC-1234"
    path: "/docs/contribute/governance/rfcs/1234_my_rfc.md"
`,
				},
				{
					path: "docs/contribute/governance/rfcs/_rfcs.yaml",
					content: `
- name: 'RFC-1234'
  title: 'My RFC'
  short_description: "This RFC is mine. You can't have it."
  authors: ['page@google.com']
  file: '1234_my_rfc.md'
`,
				},
				{
					path:    "docs/contribute/governance/rfcs/1234_my_rfc.md",
					content: ``,
				},
			},
			expected: []*staticanalysis.Finding{
				{
					Category: "rfcmeta/file/rfcid_tag_not_found",
					Message:  "No `{% set rfcid = \"RFC-1234\" %}` tag found.",
					Path:     "docs/contribute/governance/rfcs/1234_my_rfc.md",
				},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			checkoutDir := t.TempDir()
			if err := os.MkdirAll(filepath.Join(checkoutDir, "docs/contribute/governance/rfcs"), 0o700); err != nil {
				t.Fatal(err)
			}

			analyzer := analyzer{checkoutDir: checkoutDir}

			for _, file := range test.files {
				if err := os.WriteFile(filepath.Join(checkoutDir, file.path), []byte(file.content), 0o600); err != nil {
					t.Fatal(err)
				}
			}

			findings, err := analyzer.Analyze(context.Background(), test.path)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(test.expected, findings, cmpopts.EquateEmpty()); diff != "" {
				t.Errorf("Analyzer diff (-want +got):\n%s", diff)
			}
		})
	}
}
