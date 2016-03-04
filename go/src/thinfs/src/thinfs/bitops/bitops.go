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
