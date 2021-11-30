// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/bitsstrictflexible"
)

// [START contents]
func useBits(bits lib.Flags) uint32 {
	if bits.HasUnknownBits() {
		return uint32(bits.GetUnknownBits())
	}
	var result lib.Flags = 0
	if bits.HasBits(lib.FlagsOptionA) {
		result |= lib.Flags_Mask
	}
	return uint32(result)
}

// [END contents]

func main() {}
