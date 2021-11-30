// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/bitsmemberremove"
	"fmt"
)

// [START contents]
func useBits(bits lib.Flags) {
	if bits.HasBits(lib.FlagsOptionA) {
		fmt.Println("option C is set")
	}
	if bits.HasBits(lib.FlagsOptionB) {
		fmt.Println("option C is set")
	}
	if bits.HasUnknownBits() {
		fmt.Printf("unknown options: 0x%x", bits.GetUnknownBits())
	}
}

// [END contents]

func main() {}
