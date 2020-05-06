// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

type fidlType string

const (
	uint8Type   fidlType = "uint8"
	uint16Type  fidlType = "uint16"
	uint32Type  fidlType = "uint32"
	uint64Type  fidlType = "uint64"
	int8Type    fidlType = "int8"
	int16Type   fidlType = "int16"
	int32Type   fidlType = "int32"
	int64Type   fidlType = "int64"
	float32Type fidlType = "float32"
	float64Type fidlType = "float64"
)
