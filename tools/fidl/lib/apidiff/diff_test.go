// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

func TestCmp(t *testing.T) {
	tests := []struct {
		one, other summarize.ElementStr
		expected   int
	}{
		{
			one:      summarize.ElementStr{Name: "aa"},
			other:    summarize.ElementStr{Name: "aa"},
			expected: 0,
		},
		{
			one:      summarize.ElementStr{Name: "aa"},
			other:    summarize.ElementStr{Name: "bb"},
			expected: -1,
		},
		{
			one:      summarize.ElementStr{Name: "bb"},
			other:    summarize.ElementStr{Name: "aa"},
			expected: 1,
		},
	}
	for _, test := range tests {
		actual := cmpFn(test.one, test.other)
		if test.expected != actual {
			t.Errorf("want: %+v, got: %v for one=%v, other=%v",
				test.expected, actual, test.one, test.other)
		}
	}
}

func TestIter(t *testing.T) {
	t.Parallel()
	tests := []struct {
		before, after []summarize.ElementStr
		expected      [][]*summarize.ElementStr
	}{
		{
			before: []summarize.ElementStr{
				{Name: "aa"},
			},
			after: []summarize.ElementStr{},
			expected: [][]*summarize.ElementStr{
				{
					&summarize.ElementStr{Name: "aa"},
					nil,
				},
			},
		},
		{
			before: []summarize.ElementStr{},
			after: []summarize.ElementStr{
				{Name: "aa"},
			},
			expected: [][]*summarize.ElementStr{
				{
					nil,
					&summarize.ElementStr{Name: "aa"},
				},
			},
		},
		{
			before: []summarize.ElementStr{
				{Name: "aa"},
			},
			after: []summarize.ElementStr{
				{Name: "bb"},
			},
			expected: [][]*summarize.ElementStr{
				{
					&summarize.ElementStr{Name: "aa"},
					nil,
				},
				{
					nil,
					&summarize.ElementStr{Name: "bb"},
				},
			},
		},
		{
			before: []summarize.ElementStr{
				{Name: "aa"},
			},
			after: []summarize.ElementStr{
				{Name: "aa"},
			},
			expected: [][]*summarize.ElementStr{
				{
					&summarize.ElementStr{Name: "aa"},
					&summarize.ElementStr{Name: "aa"},
				},
			},
		},
		{
			before: []summarize.ElementStr{
				{Name: "aa"},
				{Name: "bb"},
				{Name: "cc"},
			},
			after: []summarize.ElementStr{
				{Name: "bb"},
				{Name: "cc"},
				{Name: "dd"},
			},
			expected: [][]*summarize.ElementStr{
				{
					&summarize.ElementStr{Name: "aa"},
					nil,
				},
				{
					&summarize.ElementStr{Name: "bb"},
					&summarize.ElementStr{Name: "bb"},
				},
				{
					&summarize.ElementStr{Name: "cc"},
					&summarize.ElementStr{Name: "cc"},
				},
				{
					nil,
					&summarize.ElementStr{Name: "dd"},
				},
			},
		},
		{
			before: []summarize.ElementStr{
				{Name: "aa"},
				{Name: "bb"},
				{Name: "cc"},
				{Name: "dd"},
			},
			after: []summarize.ElementStr{
				{Name: "bb"},
				{Name: "cc"},
				{Name: "dd"},
				{Name: "ee"},
				{Name: "ff"},
			},
			expected: [][]*summarize.ElementStr{
				{
					&summarize.ElementStr{Name: "aa"},
					nil,
				},
				{
					&summarize.ElementStr{Name: "bb"},
					&summarize.ElementStr{Name: "bb"},
				},
				{
					&summarize.ElementStr{Name: "cc"},
					&summarize.ElementStr{Name: "cc"},
				},
				{
					&summarize.ElementStr{Name: "dd"},
					&summarize.ElementStr{Name: "dd"},
				},
				{
					nil,
					&summarize.ElementStr{Name: "ee"},
				},
				{
					nil,
					&summarize.ElementStr{Name: "ff"},
				},
			},
		},
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("t-%d", i), func(t *testing.T) {
			actual := [][]*summarize.ElementStr{}
			parallelIter(test.before, test.after,
				func(before, after *summarize.ElementStr) {
					actual = append(
						actual,
						[]*summarize.ElementStr{before, after})
				})
			if !cmp.Equal(test.expected, actual) {
				t.Errorf("want:\n\t%+v\ngot:\n\t%+v\n", test.expected, actual)

			}
		})
	}
}
