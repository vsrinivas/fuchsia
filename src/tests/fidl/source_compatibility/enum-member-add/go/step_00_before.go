// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/enummemberadd"
)

// [START contents]
func writer(s string) lib.Color {
	switch s {
	case "blue":
		return lib.ColorBlue
	case "red":
		return lib.ColorRed
	default:
		return lib.Color_Unknown
	}
}

func reader(color lib.Color) string {
	switch color {
	case lib.ColorBlue:
		return "blue"
	case lib.ColorRed:
		return "red"
	default:
		return "<unknown>"
	}
}

// [END contents]

func main() {}
