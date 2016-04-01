// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
