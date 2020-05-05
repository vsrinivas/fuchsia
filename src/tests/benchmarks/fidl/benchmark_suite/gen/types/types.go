// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

type FidlType string

const (
	Uint8   FidlType = "uint8"
	Uint16  FidlType = "uint16"
	Uint32  FidlType = "uint32"
	Uint64  FidlType = "uint64"
	Int8    FidlType = "int8"
	Int16   FidlType = "int16"
	Int32   FidlType = "int32"
	Int64   FidlType = "int64"
	Float32 FidlType = "float32"
	Float64 FidlType = "float64"
)
