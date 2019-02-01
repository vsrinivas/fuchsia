// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package bitops provides various utility functions and types for low level
// bit-twiddling.
package bitops

// FFS (Find First Set) returns the index of the least significant bit in x
// that is set to 1.  Calling FFS(0) is undefined behavior.
func FFS(x uint64) (n uint)

var ffsTable = []uint{0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0}

func ffs(x uint64) uint {
	var n uint

	if x&0x00000000FFFFFFFF == 0 {
		n += 32
		x >>= 32
	}
	if x&0x000000000000FFFF == 0 {
		n += 16
		x >>= 16
	}
	if x&0x00000000000000FF == 0 {
		n += 8
		x >>= 8
	}
	if x&0x000000000000000F == 0 {
		n += 4
		x >>= 4
	}
	n += ffsTable[x&0xf]

	return n
}

// CLZ (Count Leading Zeros) counts the number of leading zeroes before the
// most significant bit in x that is set to 1.  Calling CLZ(0) is undefined
// behavior.
func CLZ(x uint64) (n uint)

var clzTable = []uint{4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0}

func clz(x uint64) uint {
	var n uint

	if x&0xFFFFFFFF00000000 == 0 {
		n += 32
		x <<= 32
	}
	if x&0xFFFF000000000000 == 0 {
		n += 16
		x <<= 16
	}
	if x&0xFF00000000000000 == 0 {
		n += 8
		x <<= 8
	}
	if x&0xF000000000000000 == 0 {
		n += 4
		x <<= 4
	}
	n += clzTable[x>>60]

	return n
}
