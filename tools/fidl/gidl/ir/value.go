// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

// Value represents any acceptable value used to represent a FIDL value.
// This type may wrap one of:
// - `string` for strings
// - `int64` for negative numbers (of any size), bits, and enums
// - `uint64` for positive numbers (of any size), bits, and enums
// - `float64` for floating point numbers (of any size)
// - `bool` for booleans
// - `Handle` for handles
// - `Record` for structs, tables, and unions
// - `[]interface{}` for slices of values
// - `nil` for null values (only allowed for nullable types)
// - `UnknownData` for unknown variants of unions
type Value interface{}

// A Handle is an index into the test's []HandleDef.
type Handle uint64

// Record represents a value for a struct, table, or union type.
type Record struct {
	// Unqualified type name.
	Name string
	// List of fields. Struct and table records can have any number of fields.
	// Union records should have exactly one field.
	Fields []Field
}

// Field represents a field in a struct, table, or union value.
type Field struct {
	Key   FieldKey
	Value Value
}

// FieldKey designates a field in a struct, table, or union type. The key is
// either known (represented by name) or unknown (represented by ordinal).
//
// Only flexible tables and flexible unions can have unknown keys. Although
// known table/union fields (strict or flexible) have both a name and ordinal,
// FieldKey only stores the name.
type FieldKey struct {
	Name           string
	UnknownOrdinal uint64
}

// IsKnown returns true if f is a known (i.e. named) key.
func (f *FieldKey) IsKnown() bool {
	return f.Name != ""
}

// IsUnknown returns true if f is an unknown (i.e. ordinal) key.
func (f *FieldKey) IsUnknown() bool {
	return f.Name == ""
}

// UnknownData represents the raw payload of an envelope, e.g. the data
// corresponding to an unknown variant of a union
type UnknownData struct {
	Bytes   []byte
	Handles []Handle
}
