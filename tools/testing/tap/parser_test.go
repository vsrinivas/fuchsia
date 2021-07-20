// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap

import (
	"reflect"
	"strings"
	"testing"
)

func TestParse(t *testing.T) {
	tests := []struct {
		name     string
		input    string
		expected *Document
	}{
		{
			name:  "should parse a document containing only the version",
			input: strings.TrimSpace(`TAP version 13`),
			expected: &Document{
				Version: 13,
			},
		},
		{
			name: "should parse a document containing only the version and plan",
			input: strings.TrimSpace(`
TAP version 13
1..2
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 2},
			},
		},
		{
			name: "should parse a basic TAP document",
			input: strings.TrimSpace(`
TAP version 13
1..4
ok 1 - This test passed
ok 2 # TODO this test is disabled
not ok 3 - This test failed
ok 4 - This test passed also # TODO congratulate the author
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 4},
				TestLines: []TestLine{
					{Ok: true, Count: 1, Description: "- This test passed"},
					{Ok: true, Count: 2, Directive: Todo, Explanation: "this test is disabled"},
					{Ok: false, Count: 3, Description: "- This test failed"},
					{Ok: true, Count: 4, Description: "- This test passed also", Directive: Todo, Explanation: "congratulate the author"},
				},
			},
		},
		{
			name: "should parse a plan at the end of the document",
			input: strings.TrimSpace(`
TAP version 13
ok 1 - This test passed
ok 2 # TODO this test is disabled
not ok 3 - This test failed
1..3
`),
			expected: &Document{
				Version: 13,
				Plan: Plan{
					Start: 1,
					End:   3,
				},
				TestLines: []TestLine{
					{Ok: true, Count: 1, Description: "- This test passed"},
					{Ok: true, Count: 2, Directive: Todo, Explanation: "this test is disabled"},
					{Ok: false, Count: 3, Description: "- This test failed"},
				},
			},
		},
		{
			name: "should skip garbage output",
			input: strings.TrimSpace(`
TAP version 13
ERROR: segfault at 0x33123. print stackdump;
0x00001fff: 0x88881
0x00001ffe: 0x88881
0x00001ffd: 0x88881
1..3
0x00001ffc: 0x88881
ok 1 - This test passed
ok 2 # TODO this test is disabled
exiting
not ok 3 - This test failed
`),
			expected: &Document{
				Version: 13,
				Plan: Plan{
					Start: 1,
					End:   3,
				},
				TestLines: []TestLine{
					{Ok: true, Count: 1, Description: "- This test passed"},
					{Ok: true, Count: 2, Directive: Todo, Explanation: "this test is disabled"},
					{Ok: false, Count: 3, Description: "- This test failed"},
				},
			},
		},
		{
			name: "should skip a line with an incomplete test plan",
			input: strings.TrimSpace(`
TAP version 13
1..
not ok 3 - This test failed
`),
			expected: &Document{
				Version: 13,
				TestLines: []TestLine{
					{Ok: false, Count: 3, Description: "- This test failed"},
				},
			},
		},
		{
			name: "should preserve spaces in description",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1 - This test     passed
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{Ok: true, Count: 1, Description: "- This test     passed"},
				},
			},
		},
		{
			name: "should preserve spaces in directive explanation",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1 # SKIP this  is   disabled
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{Ok: true, Count: 1, Directive: Skip, Explanation: "this  is   disabled"},
				},
			},
		},
		{
			name: "should parse a YAML block",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
 ---
 name: foo
 start: 1
 end: 2
 ...
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{
						Ok:    true,
						Count: 1,
						YAML:  "name: foo\nstart: 1\nend: 2\n",
					},
				},
			},
		},
		{
			name: "should parse a YAML block whose header contains trailing characters",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
 ---trailing chars
 name: foo
 start: 1
 end: 2
 ...
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{
						Ok:    true,
						Count: 1,
						YAML:  "name: foo\nstart: 1\nend: 2\n",
					},
				},
			},
		},
		{
			name: "should parse a YAML block whose footer contains trailing characters",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
 ---
 name: foo
 start: 1
 end: 2
 ...trailing chars
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{
						Ok:    true,
						Count: 1,
						YAML:  "name: foo\nstart: 1\nend: 2\n",
					},
				},
			},
		},
		{
			name: "should parse a YAML block whose header and footer contain trailing characters",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
 --- trailing chars
 name: foo
 start: 1
 end: 2
 ... even more trailing chars
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{
						Ok:    true,
						Count: 1,
						YAML:  "name: foo\nstart: 1\nend: 2\n",
					},
				},
			},
		},
		{
			name: "should skip a YAML block at the start of the output",
			input: strings.TrimSpace(`
TAP version 13
1..1
	---
	name: foo
	start: 1
	end: 2
	...
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
			},
		},
		{
			name: "should skip a YAML block that does not follow a test line",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
	---
	name: foo_test
	start: 1
	end: 2
	...
	---
	name: bar_test
	start: 3
	end: 4
	...
`),
			expected: &Document{
				Version:   13,
				Plan:      Plan{Start: 1, End: 1},
				TestLines: []TestLine{{Ok: true, Count: 1, YAML: "name: foo_test\nstart: 1\nend: 2\n"}},
			},
		},
		{
			name: "should parse a YAML block with no trailing /\\s+.../",
			input: strings.TrimSpace(`
TAP version 13
1..1
ok 1
  ---
  name: foo_test
  start: 1
  end: 2
`),
			expected: &Document{
				Version: 13,
				Plan:    Plan{Start: 1, End: 1},
				TestLines: []TestLine{
					{Ok: true, Count: 1, YAML: "name: foo_test\nstart: 1\nend: 2\n"},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			doc, err := Parse([]byte(tt.input))
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(doc, tt.expected) {
				t.Errorf("got\n%+v\nbut wanted\n%+v", doc, tt.expected)
			}
		})
	}
}
