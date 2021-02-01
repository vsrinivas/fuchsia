// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"testing"
)

func doOptimalBlockAlign(t *testing.T, first, bytesize, logical, physical, expectedStart, expectedEnd uint64) {
	start, end := optimalBlockAlign(first, bytesize, logical, physical /*optimal=*/, 0)

	if start != expectedStart || end != expectedEnd {
		t.Fatalf("optimalBlockAlign(first=%d, bytesize=%d, logical=%d, physical=%d, optimal=0) was wrong! Got start=%d end=%d, expected start=%d end=%d", first, bytesize, logical, physical, start, end, expectedStart, expectedEnd)
	}
}

func TestOptimalBlockAlign(t *testing.T) {
	// Basic tests.
	doOptimalBlockAlign(t, 0, 4096, 512, 4096, 0, 7)
	doOptimalBlockAlign(t, 1, 4096, 512, 4096, 8, 15)
	doOptimalBlockAlign(t, 1, 4096, 512, 4096, 8, 15)

	// Tiny partitions.
	doOptimalBlockAlign(t, 1, 1, 512, 4096, 8, 8)
	doOptimalBlockAlign(t, 1, 1, 512, 4096, 8, 8)
	doOptimalBlockAlign(t, 79, 30, 512, 4096, 80, 80)

	// Strange logical/physical block sizes. Note that we always use powers of two.
	doOptimalBlockAlign(t, 3, 50, 8, 128, 16, 22)
	doOptimalBlockAlign(t, 4, 3270, 64, 256, 4, 55)

	// A more realistic test somewhere in the middle of a disk.
	doOptimalBlockAlign(t, 2696965, 509393622, 512, 4096, 2696968, 3691877)
}
