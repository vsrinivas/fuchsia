// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// fidl_api_summarize tests package.
package main

import (
	"flag"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	fidl_testing "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_testing"
)

func TestSummarize(t *testing.T) {
	flag.Parse()
	tests := []struct {
		name     string
		fidl     string
		expected string
	}{
		{
			name: "library only",
			fidl: `library l;`,
			expected: `library l
`,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			r := fidl_testing.EndToEndTest{T: t}.Single(test.fidl)
			var sb strings.Builder
			if err := Summarize(r, &sb); err != nil {
				t.Fatalf("while summarizing file: %v", err)
			}
			actual := strings.Split(sb.String(), "\n")
			expected := strings.Split(test.expected, "\n")

			if !cmp.Equal(expected, actual) {
				t.Errorf("expected:\n%+v\n\tactual:\n%+v\n\tdiff:\n%v",
					test.expected, sb.String(),
					cmp.Diff(expected, actual))
			}
		})
	}
}
