// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/unionmemberremove"
	"fmt"
	"strconv"
)

// [START contents]
func writer(s string) lib.JsonValue {
	n, err := strconv.ParseInt(s, 10, 32)
	if err == nil {
		return lib.JsonValueWithIntValue(int32(n))
	}
	return lib.JsonValueWithStringValue(s)
}

func reader(value lib.JsonValue) string {
	switch value.Which() {
	case lib.JsonValueIntValue:
		return fmt.Sprintf("%d", value.IntValue)
	case lib.JsonValueStringValue:
		return value.StringValue
	default:
		return fmt.Sprintf("<%d unknown bytes>", len(value.GetUnknownData().Bytes))
	}
}

// [END contents]

func main() {}
