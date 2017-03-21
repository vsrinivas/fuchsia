// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitops

import (
	"testing"
)

func TestFfs(t *testing.T) {
	// Defined in common_test.go.  Creates a new random number generator.
	r := setUp(t)

	for i := 0; i < 100; i++ {
		x := uint64(r.Int63())
		if x == 0 {
			// Just skip zeroes.
			continue
		}
		expected := ffs(x)
		if actual := FFS(x); actual != expected {
			t.Errorf("FFS(%#x) = %v; want %v\n", x, actual, expected)
		}
	}
}

func TestClz(t *testing.T) {
	// Defined in common_test.go.  Creates a new random number generator.
	r := setUp(t)

	for i := 0; i < 100; i++ {
		x := uint64(r.Int63())
		if x == 0 {
			// Just skip zeroes.
			continue
		}
		expected := clz(x)
		if actual := CLZ(x); actual != expected {
			t.Errorf("CLZ(%#x) = %v; want %v\n", x, actual, expected)
		}
	}
}
