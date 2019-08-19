// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type All struct {
	Success       []Success
	EncodeSuccess       []EncodeSuccess
	DecodeSuccess       []DecodeSuccess
	EncodeFailure []EncodeFailure
	DecodeFailure []DecodeFailure
}

type Success struct {
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type EncodeSuccess struct {
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type DecodeSuccess struct {
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type EncodeFailure struct {
	Name              string
	Value             interface{}
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
}

type DecodeFailure struct {
	Name              string
	Type              string
	Bytes             []byte
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
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

// TODO(34770) Organize error codes by encoding / decoding.
// Potentially do a check in the parser that the code is the right type.
const (
	_                           ErrorCode = ""
	StringTooLong                         = "STRING_TOO_LONG"
	NullEmptyStringWithNullBody           = "NON_EMPTY_STRING_WITH_NULL_BODY"
	StrictXUnionFieldNotSet               = "STRICT_XUNION_FIELD_NOT_SET"
	StrictXUnionUnknownField              = "STRICT_XUNION_UNKNOWN_FIELD"
)

var AllErrorCodes = map[ErrorCode]bool{
	StringTooLong:               true,
	NullEmptyStringWithNullBody: true,
	StrictXUnionFieldNotSet:     true,
	StrictXUnionUnknownField:    true,
}
