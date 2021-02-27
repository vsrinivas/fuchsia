// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	lib "fidl/fidl/test/tablememberadd"
	"fmt"
)

// [START contents]
func useTable(profile lib.Profile) {
	if profile.HasTimezone() {
		fmt.Printf("timezone: %s", profile.GetTimezone())
	}
	if profile.HasTemperatureUnit() {
		fmt.Printf("preferred unit: %s", profile.GetTemperatureUnit())
	}
	for ord, data := range profile.GetUnknownData() {
		fmt.Printf("unknown ordinal %d with bytes %v", ord, data.Bytes)
	}
}

// [END contents]

func main() {}
