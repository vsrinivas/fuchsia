// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"math"
	"testing"
)

func TestStackOfBoundsTag(t *testing.T) {
	cases := []struct {
		input    []int
		expected string
	}{
		{
			input:    []int{},
			expected: "",
		},
		{
			input:    []int{1, 2, 3},
			expected: "3,2,1",
		},
		{
			input:    []int{math.MaxInt32, 1, math.MaxInt32},
			expected: ",1,",
		},
	}
	for _, ex := range cases {
		actual := StackOfBoundsTag{ex.input}.String()
		if actual != ex.expected {
			t.Errorf("%v: expected '%s', actual '%s'", ex.input, ex.expected, actual)
		}
	}
}
