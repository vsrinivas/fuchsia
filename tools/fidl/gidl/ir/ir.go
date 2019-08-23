// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type All struct {
	EncodeSuccess []EncodeSuccess
	DecodeSuccess []DecodeSuccess
	EncodeFailure []EncodeFailure
	DecodeFailure []DecodeFailure
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

func Merge(input []All) All {
	var output All
	for _, elem := range input {
		for _, encodeSuccess := range elem.EncodeSuccess {
			output.EncodeSuccess = append(output.EncodeSuccess, encodeSuccess)
		}
		for _, decodeSuccess := range elem.DecodeSuccess {
			output.DecodeSuccess = append(output.DecodeSuccess, decodeSuccess)
		}
		for _, encodeFailure := range elem.EncodeFailure {
			output.EncodeFailure = append(output.EncodeFailure, encodeFailure)
		}
		for _, decodeFailure := range elem.DecodeFailure {
			output.DecodeFailure = append(output.DecodeFailure, decodeFailure)
		}
	}
	return output
}

func FilterByBinding(input All, binding string) All {
	shouldKeep := func(binding string, allowlist *[]string, denylist *[]string) bool {
		if denylist != nil {
			for _, item := range *denylist {
				if binding == item {
					return false
				}
			}
		}
		if allowlist != nil {
			for _, item := range *allowlist {
				if binding == item {
					return true
				}
			}
			return false
		}
		return true
	}
	var output All
	for _, def := range input.EncodeSuccess {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeSuccess = append(output.EncodeSuccess, def)
		}
	}
	for _, def := range input.DecodeSuccess {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeSuccess = append(output.DecodeSuccess, def)
		}
	}
	for _, def := range input.EncodeFailure {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeFailure = append(output.EncodeFailure, def)
		}
	}
	for _, def := range input.DecodeFailure {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeFailure = append(output.DecodeFailure, def)
		}
	}
	return output
}
