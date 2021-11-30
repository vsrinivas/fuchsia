// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/unionflexiblestrict"
	"fmt"
)

// [START contents]
func useUnion(value lib.JsonValue) {
	switch value.Which() {
	case lib.JsonValueIntValue:
		fmt.Printf("int value: %d\n", value.IntValue)
	case lib.JsonValueStringValue:
		fmt.Printf("string value: %s\n", value.StringValue)
	default:
		fmt.Println("<unknown tag>")
	}
}

// [END contents]

func main() {}
