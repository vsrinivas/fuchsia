// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"errors"
	lib "fidl/fidl/test/enumstrictflexible"
)

// [START contents]
func writer(s string) lib.Color {
	switch {
	case s == "blue":
		return lib.ColorBlue
	case s == "red":
		return lib.ColorRed
	default:
		return lib.ColorUnknownColor
	}
}

func reader(color lib.Color) (string, error) {
	switch color {
	case lib.ColorBlue:
		return "blue", nil
	case lib.ColorRed:
		return "red", nil
	case lib.ColorUnknownColor:
		return "unknown", nil
	default:
		return "", errors.New("invalid color enum")
	}
}

// [END contents]

func main() {}
