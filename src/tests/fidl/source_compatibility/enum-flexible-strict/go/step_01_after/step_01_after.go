// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/enumflexiblestrict"
)

// [START contents]
func complement(color lib.Color) lib.Color {
	switch color {
	case lib.ColorBlue:
		return lib.ColorRed
	case lib.ColorRed:
		return lib.ColorBlue
	default:
		return color
	}
}

// [END contents]

func main() {}
