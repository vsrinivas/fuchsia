// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitmap

import "testing"

func checkAllocate(t *testing.T, bm *Bitmap, expected []uint32) {
	count := len(expected)
	allocated, err := bm.Allocate(uint32(count))
	if err != nil {
		t.Fatal(err)
	} else if count != len(allocated) {
		t.Fatalf("Requested %d bits, got %d bits", count, len(allocated))
	}
	for i := range allocated {
		if allocated[i] != expected[i] {
			t.Fatalf("Unexpected allocation at %d (expected %d)", allocated[i], expected[i])
		}
	}
}

func checkBitmapEquals(t *testing.T, bm *Bitmap, expected []int) {
	for i := range expected {
		e := true
		if expected[i] == 0 {
			e = false
		}
		if bm.Get(uint32(i)) != e {
			t.Fatalf("Unexpected value at index %d", i)
		}
	}
}

// Test single-byte bitmap
func TestSmallBitmap(t *testing.T) {
	buf := make([]byte, 1)
	lo, hi := uint32(2), uint32(8)
	bm := New(buf, lo, hi)

	checkBitmapEquals(t, bm, []int{0, 0, 0, 0, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{2, 3})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{4, 5, 6, 7})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 1})

	if _, err := bm.Allocate(1); err == nil {
		t.Fatal("The bitmap should have been full")
	}

	bm.Free([]uint32{4, 6})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 1, 0, 1})
}

func TestMultiByteBitmap(t *testing.T) {
	buf := make([]byte, 2)
	lo, hi := uint32(2), uint32(12)
	bm := New(buf, lo, hi)

	checkBitmapEquals(t, bm, []int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{2, 3})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{4, 5, 6, 7, 8})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{9})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0})
	bm.Free([]uint32{4, 8})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0})
	bm.Free([]uint32{4}) // Test freeing something that is already free
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0})
	bm.Free(nil) // Test an empty free
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0})

	// Test copy
	copyBuf := bm.Copy()
	bmCopy := New(copyBuf, lo, hi)
	checkBitmapEquals(t, bmCopy, []int{0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0})
}

func TestForceWrapAround(t *testing.T) {
	buf := make([]byte, 2)
	lo, hi := uint32(2), uint32(12)
	bm := New(buf, lo, hi)

	checkBitmapEquals(t, bm, []int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{2, 3, 4, 5, 6, 7, 8, 9, 10, 11})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0})
	bm.Free([]uint32{7, 8, 9})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0})
	checkAllocate(t, bm, []uint32{7, 8, 9})
	checkBitmapEquals(t, bm, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0})
}
