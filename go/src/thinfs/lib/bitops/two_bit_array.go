// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitops

// TwoBitArray is a bit array with two bits per entry.  Within each byte in the
// TwoBitArray, position 0 points to the least significant 2 bits while position 3
// points to the most significant 2 bits.
type TwoBitArray []byte

// Set sets the value of the 2 bits at position pos to the value of the lowest 2 bits
// in val.
func (tba TwoBitArray) Set(pos uint, val byte) {
	off := 2 * (pos % 4)
	data := (val & 0x3) << off
	mask := ^(byte(0x3) << off)

	idx := pos / 4 // 2*pos / 8
	tba[idx] = (tba[idx] & mask) | data
}

// Get returns the value of the 2 bits at the position pos in the lowest 2 bits of
// returned byte.
func (tba TwoBitArray) Get(pos uint) byte {
	idx := pos / 4 // 2*pos / 8
	off := 2 * (pos % 4)

	return (tba[idx] >> off) & 0x3
}

// TwoBitArrayLength returns the length necessary for a byte slice to hold a
// TwoBitArray with count elements.
func TwoBitArrayLength(count int) int {
	if count%4 == 0 {
		return count / 4
	}

	return (count / 4) + 1
}
