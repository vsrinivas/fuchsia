// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitops

import (
	"testing"
)

func TestTwoBitArray(t *testing.T) {
	// Defined in common_test.go.  Creates a new random number generator.
	r := setUp(t)

	const size = 113
	expected := make([]byte, size)
	array := TwoBitArray(make([]byte, TwoBitArrayLength(size)))

	for i := range expected {
		val := byte(r.Intn(4))
		expected[i] = val
		array.Set(uint(i), val)
	}

	for i := range expected {
		if actual := array.Get(uint(i)); actual != expected[i] {
			t.Errorf("array.Get(%v) = %v; want %v\n", i, actual, expected[i])
		}
	}

	// Update and check random positions.
	for i := 0; i < 100; i++ {
		idx := uint(r.Intn(size))
		if r.Int()&0x1 == 0 {
			val := byte(r.Intn(4))
			expected[idx] = val
			array.Set(idx, val)
		} else {
			if actual := array.Get(idx); actual != expected[idx] {
				t.Errorf("array.Get(%v) = %v; want %v\n", i, actual, expected[i])
			}
		}
	}
}
