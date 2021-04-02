// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"testing"
)

func TestWraparoundTypeSerialization(t *testing.T) {
	tests := []struct {
		input    wraparoundType
		expected string
	}{
		{
			input: wraparoundType{
				named: named{
					name:   "foo",
					parent: "bar",
				},
				subtype:    "baz",
				strictness: true,
				parentType: "parentType",
			},
			expected: "strict parentType bar.foo baz",
		},
		{
			input: wraparoundType{
				named: named{
					name:   "foo",
					parent: "bar",
				},
				subtype:    "baz",
				strictness: false,
				parentType: "parentType",
			},
			expected: "flexible parentType bar.foo baz",
		},
	}
	for _, test := range tests {
		actual := test.input.Serialize().String()
		if test.expected != actual {
			t.Errorf("want: %+v, got: %+v", test.expected, actual)
		}
	}
}
