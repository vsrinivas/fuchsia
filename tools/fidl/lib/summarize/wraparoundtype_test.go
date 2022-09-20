// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestWraparoundTypeSerialization(t *testing.T) {
	tests := []struct {
		name     string
		input    wraparoundType
		expected ElementStr
	}{
		{
			name: "strict wraparound",
			input: wraparoundType{
				named: named{
					name:   "foo",
					parent: "bar",
				},
				subtype:    "baz",
				strictness: true,
				parentType: "parentType",
			},
			expected: ElementStr{
				Type:       "baz",
				Kind:       "parentType",
				Name:       "bar.foo",
				Strictness: isStrict,
			},
		},
		{
			name: "flexible wraparound",
			input: wraparoundType{
				named: named{
					name:   "foo",
					parent: "bar",
				},
				subtype:    "baz",
				strictness: false,
				parentType: "parentType",
			},
			expected: ElementStr{
				Type:       "baz",
				Kind:       "parentType",
				Name:       "bar.foo",
				Strictness: isFlexible,
			},
		},
	}
	for _, test := range tests {
		actual := test.input.Serialize()
		if diff := cmp.Diff(test.expected, actual); diff != "" {
			t.Errorf("%s: expected != actual (-want +got)\n%s", test.name, diff)
		}
	}
}
