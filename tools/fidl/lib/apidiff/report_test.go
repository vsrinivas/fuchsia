// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	yaml "gopkg.in/yaml.v2"
)

var (
	ignoreUnexported = cmpopts.IgnoreUnexported(Report{})
)

func TestReportYAML(t *testing.T) {
	t.Parallel()
	tests := []string{
		`
api_diff:
- name: foo
  conclusion: Transitionable
`,
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("t-%d", i), func(t *testing.T) {
			var report Report
			if err := yaml.Unmarshal(
				[]byte(test), &report); err != nil {
				t.Fatalf("while decoding yaml: %v: %+v", err, test)
			}
			var sb strings.Builder
			if err := report.WriteText(&sb); err != nil {
				t.Fatalf("while writing text: %v", err)
			}
			var report2 Report
			if err := yaml.Unmarshal(
				[]byte(sb.String()), &report2); err != nil {
				t.Fatalf("while decoding yaml: %v: %+v", err, sb.String())
			}
			if !cmp.Equal(report, report2, cmpOptions) {
				t.Errorf("want:\n%+v\n\ngot:\n%+v\n\ndiff:\n%v",
					report, report2, cmp.Diff(report, report2))

			}
		})
	}
}

func TestReportJSON(t *testing.T) {
	t.Parallel()
	tests := []string{`
{
  "api_diff": [
    {
      "name": "foo",
      "conclusion": "Transitionable"
    }
  ]
}
`,
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("t-%d", i), func(t *testing.T) {
			var report Report
			if err := json.Unmarshal(
				[]byte(test), &report); err != nil {
				t.Fatalf("while decoding json: %v: %+v", err, test)
			}
			var sb strings.Builder
			if err := report.WriteJSON(&sb); err != nil {
				t.Fatalf("while writing text: %v", err)
			}
			var report2 Report
			if err := json.Unmarshal(
				[]byte(sb.String()), &report2); err != nil {
				t.Fatalf("while decoding json: %v: %+v", err, sb.String())
			}
			if !cmp.Equal(report, report2, cmpOptions) {
				t.Errorf("want:\n%v\n\ngot:\n%v\n\ndiff:\n%v", report, report2, cmp.Diff(report, report2, cmpOptions))

			}

		})
	}
}
