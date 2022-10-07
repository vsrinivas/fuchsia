// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Strictness describes if an element is strict or flexible:
// https://fuchsia.dev/fuchsia-src/reference/fidl/language/language#strict-vs-flexible
type Strictness string

var (
	noStrictness Strictness = ""
	isStrict     Strictness = "strict"
	isFlexible   Strictness = "flexible"
)

// Resourceness describes if an element is a value or resource type:
// https://fuchsia.dev/fuchsia-src/reference/fidl/language/language#value-vs-resource
type Resourceness string

var (
	// isValue is the default, so we omit it altogether.
	isValue Resourceness = ""
	// isResource means the type is allowed to contain handles.
	isResource Resourceness = "resource"
)

// Kind corresponds to fidl::flat::Element::Kind in fidlc, e.g. const, enum,
// enum member, struct, struct member, protocol, method, etc.
type Kind string

// Kind constants.
const (
	AliasKind          Kind = "alias"
	BitsKind           Kind = "bits"
	BitsMemberKind     Kind = "bits/member"
	ConstKind          Kind = "const"
	EnumKind           Kind = "enum"
	EnumMemberKind     Kind = "enum/member"
	LibraryKind        Kind = "library"
	ProtocolKind       Kind = "protocol"
	ProtocolMemberKind Kind = "protocol/member"
	StructKind         Kind = "struct"
	StructMemberKind   Kind = "struct/member"
	TableKind          Kind = "table"
	TableMemberKind    Kind = "table/member"
	UnionKind          Kind = "union"
	UnionMemberKind    Kind = "union/member"
)

// Type is the FIDL type of an element. For enums and bits, this is the
// underlying primitive type. For methods, this is the entire method signature.
type Type string

// Name is the fully qualified name of the element:
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0043_documentation_comment_format?hl=en#fully-qualified-names
type Name string

// Value is a string-serialized value of the element, applicable to consts,
// enum/bits members, and struct members (default values).
type Value string

// Ordinal is the ordinal of a method, or of a struct, table, or union member.
// It is a string to allow the full uint64 range in JSON. For struct members,
// which have no explicit ordinals in FIDL source, it is a one-based index.
type Ordinal string

// fidlConstToValue converts the fidlgen view of a constant value to
// summary's Value.
func fidlConstToValue(fc *fidlgen.Constant) Value {
	if fc == nil {
		return Value("")
	}
	// It looks like any value type has its value in fc.Value.
	return Value(fc.Value)
}

// ElementStr is a generic stringly-typed view of an Element. The aim is to keep
// the structure as flat as possible, and omit fields which have no bearing to
// the Kind of element represented.
//
// Keep the fields sorted by name, otherwise JSON marshaling will not match the
// fx format-code style.
type ElementStr struct {
	Kind         `json:"kind"`
	Name         `json:"name"`
	Resourceness `json:"resourceness,omitempty"`
	Strictness   `json:"strictness,omitempty"`
	Ordinal      `json:"ordinal,omitempty"`
	Type         `json:"type,omitempty"`
	Value        `json:"value,omitempty"`
}

func (e ElementStr) Less(other ElementStr) bool {
	n1 := newFqn(Name(e.Name))
	n2 := newFqn(Name(other.Name))
	return n1.Less(n2)
}

// IsStrict returns true of this element is strict. The result makes sense only
// on elements that have a defined strictness.
func (e ElementStr) IsStrict() bool {
	// Tables are always flexible
	if e.Kind == TableKind {
		return false
	}

	// Structs are always strict
	if e.Kind == StructKind {
		return true
	}

	// Some types are optionally strict.
	return e.Strictness == isStrict
}

// HasStrictness returns true if this ElementStr's FIDL kind has a notion of strictness.
//
// See https://fuchsia.dev/fuchsia-src/reference/fidl/language/language?hl=en#strict-vs-flexible.
func (e ElementStr) HasStrictness() bool {
	switch e.Kind {
	case BitsKind, EnumKind, UnionKind, StructKind, TableKind:
		return true
	default:
		return false
	}
}

// LoadSummariesJSON loads several the API summaries in the JSON format from
// the given reader readers. Returns the respective summaries in the order of
// supplied readers, or the first encountered error.
func LoadSummariesJSON(rs ...io.Reader) ([][]ElementStr, error) {
	var rets [][]ElementStr
	for _, r := range rs {
		ret := []ElementStr{}
		d := json.NewDecoder(r)
		err := d.Decode(&ret)
		if err != nil && !errors.Is(err, io.EOF) {
			return nil, fmt.Errorf("while decoding the summary: %w", err)
		}
		rets = append(rets, ret)
	}
	return rets, nil
}
