// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type All struct {
	Success       []Success
	FailsToEncode []FailsToEncode
	FailsToDecode []FailsToDecode
}

type Success struct {
	Name  string
	Value interface{}
	Bytes []byte
	// Handles
}

type FailsToEncode struct {
	Name  string
	Value interface{}
	Err   string
}

type FailsToDecode struct {
	Name  string
	Bytes []byte
	Err   string
}

// Value represents any acceptable value used to represent a FIDL value.
// This type may wrap one of:
// - `string` for strings
// - `int64` for negative numbers (of any size)
// - `uint64` for positive numbers (of any size)
// - `float64` for floating point numbers (of any size)
// - `bool` for booleans
// - `Object` for structs, unions, tables
// - `[]interface{}` for slices of values
type Value interface{}

type Object struct {
	Name   string
	Fields map[string]Value
}
