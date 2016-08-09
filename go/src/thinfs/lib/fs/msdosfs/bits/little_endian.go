// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package bits contains endianness conversion utilities.
package bits

// GetLE16 gets a 16 bit little endian number from memory.
func GetLE16(b []byte) uint16 {
	return uint16(b[1])<<8 | uint16(b[0])
}

// GetLE32 gets a 32 bit little endian number from memory.
func GetLE32(b []byte) uint32 {
	return uint32(b[3])<<24 | uint32(b[2])<<16 | uint32(b[1])<<8 | uint32(b[0])
}

// PutLE16 puts a 16 bit little endian number into memory.
func PutLE16(b []byte, v uint16) {
	b[0] = byte(v)
	b[1] = byte(v >> 8)
}

// PutLE32 puts a 32 bit little endian number into memory.
func PutLE32(b []byte, v uint32) {
	b[0] = byte(v)
	b[1] = byte(v >> 8)
	b[2] = byte(v >> 16)
	b[3] = byte(v >> 24)
}
