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

// Package util contains endianness conversion utilities.
package util

func checkLen(b []byte, l int) {
	if len(b) != l {
		panic("Invalid byte slice length")
	}
}

// GetLE16 gets a 16 bit little endian number from memory.
func GetLE16(b []byte) uint16 {
	checkLen(b, 2)
	return uint16(b[1])<<8 | uint16(b[0])
}

// GetLE32 gets a 32 bit little endian number from memory.
func GetLE32(b []byte) uint32 {
	checkLen(b, 4)
	return uint32(b[3])<<24 | uint32(b[2])<<16 | uint32(b[1])<<8 | uint32(b[0])
}

// PutLE16 puts a 16 bit little endian number into memory.
func PutLE16(b []byte, v uint16) {
	checkLen(b, 2)
	b[0] = byte(v)
	b[1] = byte(v >> 8)
}

// PutLE32 puts a 32 bit little endian number into memory.
func PutLE32(b []byte, v uint32) {
	checkLen(b, 4)
	b[0] = byte(v)
	b[1] = byte(v >> 8)
	b[2] = byte(v >> 16)
	b[3] = byte(v >> 24)
}
