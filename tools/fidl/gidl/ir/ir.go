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
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist []string
	// Handles
}

type FailsToEncode struct {
	Name              string
	Value             interface{}
	Err               ErrorCode
	BindingsAllowlist []string
}

type FailsToDecode struct {
	Name              string
	Type              string
	Bytes             []byte
	Err               ErrorCode
	BindingsAllowlist []string
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

type Field struct {
	Name  string
	Value Value
}

type Object struct {
	Name   string
	Fields []Field
}

type ErrorCode string

const (
	_                           ErrorCode = ""
	StringTooLong                         = "STRING_TOO_LONG"
	NullEmptyStringWithNullBody           = "NON_EMPTY_STRING_WITH_NULL_BODY"
)

var AllErrorCodes = map[ErrorCode]bool{
	StringTooLong:               true,
	NullEmptyStringWithNullBody: true,
}
