// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package staticanalysis

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestFindingNormalize(t *testing.T) {
	tests := []struct {
		name     string
		finding  Finding
		expected Finding
		wantErr  bool
	}{
		{
			name: "valid finding",
			finding: Finding{
				Category: "somelint/warning/not_defined",
				Message:  "variable foo is not defined",
				Path:     "src/foo/bar.cc",
			},
		},
		{
			name: "no category",
			finding: Finding{
				Category: "",
				Message:  "variable foo is not defined",
				Path:     "src/foo/bar.cc",
			},
			wantErr: true,
		},
		{
			name: "no message",
			finding: Finding{
				Category: "somelint/warning/not_defined",
				Message:  "",
				Path:     "src/foo/bar.cc",
			},
			wantErr: true,
		},
		{
			name: "relative path",
			finding: Finding{
				Category: "somelint/warning/not_defined",
				Message:  "variable foo is not defined",
				Path:     "../../src/foo/bar.cc",
			},
			wantErr: true,
		},
		{
			name: "absolute path",
			finding: Finding{
				Category: "somelint/warning/not_defined",
				Message:  "variable foo is not defined",
				Path:     "/usr/local/home/me/fuchsia/src/foo/bar.cc",
			},
			wantErr: true,
		},
		{
			name: "bad end_line",
			finding: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
				EndLine:   4,
			},
			wantErr: true,
		},
		{
			name: "bad end_char",
			finding: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
				EndLine:   5,
				StartChar: 14,
				EndChar:   1,
			},
			wantErr: true,
		},
		{
			name: "start_line unset",
			finding: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 0,
				EndLine:   5,
				StartChar: 14,
				EndChar:   20,
			},
			wantErr: true,
		},
		{
			name: "sets end_line if unset",
			finding: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
			},
			expected: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
				EndLine:   5,
			},
		},
		{
			name: "increments end_char if equal to start_char",
			finding: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
				EndLine:   5,
				StartChar: 10,
				EndChar:   10,
			},
			expected: Finding{
				Category:  "somelint/warning/not_defined",
				Message:   "variable foo is not defined",
				Path:      "src/foo/bar.cc",
				StartLine: 5,
				EndLine:   5,
				StartChar: 10,
				EndChar:   11,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if test.expected == (Finding{}) {
				// By default, expect no modifications.
				test.expected = test.finding
			}

			if err := test.finding.Normalize(); (err != nil) != test.wantErr {
				t.Errorf("Finding.Normalize() error = %q, wantErr %t", err, test.wantErr)
			}
			if diff := cmp.Diff(test.expected, test.finding); diff != "" {
				t.Errorf("Normalize caused diff (-want +got):\n%s", diff)
			}
		})
	}
}
