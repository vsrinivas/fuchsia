// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

import (
	"encoding/json"
	"fmt"
)

/*
This file contains types which describe FIDL2 interfaces.

These types are intended to be directly deserialized from the FIDL interface
JSON representation. The types are then passed directly to language-specific
generators which produce source code.

Note that these are different from a naive AST-based representation of
FIDL text. Before being transformed into JSON, FIDL sources are preprocessed
to generate metadata required by all of the backends, such as the size of
types. Importantly, this removes the need for language-specific backends to
implement field, name, or type resolution and analysis.
*/

type Identifier string

type CompoundIdentifier []Identifier

type Ordinal uint32

type PrimitiveSubtype string

const (
	Bool    PrimitiveSubtype = "bool"
	Status                   = "status"
	Int8                     = "int8"
	Int16                    = "int16"
	Int32                    = "int32"
	Int64                    = "int64"
	Uint8                    = "uint8"
	Uint16                   = "uint16"
	Uint32                   = "uint32"
	Uint64                   = "uint64"
	Float32                  = "float32"
	Float64                  = "float64"
)

type HandleSubtype string

const (
	Handle     HandleSubtype = "handle"
	Process                  = "process"
	Thread                   = "thread"
	Vmo                      = "vmo"
	Channel                  = "channel"
	Event                    = "event"
	Port                     = "port"
	Interrupt                = "interrupt"
	Iomap                    = "iomap"
	Pci                      = "pci"
	Log                      = "log"
	Socket                   = "socket"
	Resource                 = "resource"
	Eventpair                = "eventpair"
	Job                      = "job"
	Vmar                     = "vmar"
	Fifo                     = "fifo"
	Hypervisor               = "hypervisor"
	Guest                    = "guest"
	Time                     = "timer"
)

type LiteralKind string

const (
	StringLiteral  LiteralKind = "string"
	NumericLiteral             = "numeric"
	TrueLiteral                = "true"
	FalseLiteral               = "false"
	DefaultLiteral             = "default"
)

type Literal struct {
	Kind  LiteralKind `json:"kind"`
	Value string      `json:"value,omitempty"`
}

type ConstantKind string

const (
	IdentifierConstant ConstantKind = "identifier"
	LiteralConstant                 = "literal"
)

type Constant struct {
	Kind       ConstantKind       `json:"kind"`
	Identifier CompoundIdentifier `json:"identifier,omitempty"`
	Literal    Literal            `json:"literal,omitempty"`
}

type TypeKind string

const (
	ArrayType      TypeKind = "array"
	VectorType              = "vector"
	StringType              = "string"
	HandleType              = "handle"
	RequestType             = "request"
	PrimitiveType           = "primitive"
	IdentifierType          = "identifier"
)

type Type struct {
	Kind             TypeKind
	ElementType      *Type
	ElementCount     *Constant
	HandleSubtype    HandleSubtype
	RequestSubtype   CompoundIdentifier
	PrimitiveSubtype PrimitiveSubtype
	Identifier       CompoundIdentifier
	Nullable         bool
}

// UnmarshalJSON customizes the JSON unmarshalling for Type.
func (t *Type) UnmarshalJSON(b []byte) error {
	var obj map[string]*json.RawMessage
	err := json.Unmarshal(b, &obj)
	if err != nil {
		return err
	}

	err = json.Unmarshal(*obj["kind"], &t.Kind)
	if err != nil {
		return err
	}

	switch t.Kind {
	case ArrayType:
		t.ElementType = &Type{}
		err = json.Unmarshal(*obj["element_type"], t.ElementType)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["element_count"], &t.ElementCount)
		if err != nil {
			return err
		}
	case VectorType:
		t.ElementType = &Type{}
		err = json.Unmarshal(*obj["element_type"], t.ElementType)
		if err != nil {
			return err
		}
		if elementCount, ok := obj["maybe_element_count"]; ok {
			err = json.Unmarshal(*elementCount, &t.ElementCount)
			if err != nil {
				return err
			}
		}
		err = json.Unmarshal(*obj["nullability"], &t.Nullable)
		if err != nil {
			return err
		}
	case StringType:
		if elementCount, ok := obj["maybe_element_count"]; ok {
			err = json.Unmarshal(*elementCount, &t.ElementCount)
			if err != nil {
				return err
			}
		}
		err = json.Unmarshal(*obj["nullability"], &t.Nullable)
		if err != nil {
			return err
		}
	case HandleType:
		err = json.Unmarshal(*obj["subtype"], &t.HandleSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullability"], &t.Nullable)
		if err != nil {
			return err
		}
	case RequestType:
		err = json.Unmarshal(*obj["subtype"], &t.RequestSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullability"], &t.Nullable)
		if err != nil {
			return err
		}
	case PrimitiveType:
		err = json.Unmarshal(*obj["subtype"], &t.PrimitiveSubtype)
		if err != nil {
			return err
		}
	case IdentifierType:
		err = json.Unmarshal(*obj["identifier"], &t.Identifier)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullability"], &t.Nullable)
		if err != nil {
			return err
		}
	default:
		return fmt.Errorf("Unknown type kind: %s", t.Kind)
	}

	return nil
}

// Library represents a FIDL2 dependency on a separate library.
type Library struct {
	// TODO(cramertj/kulakowski)
}

// Union represents the declaration of a FIDL2 union.
type Union struct {
	Name    Identifier    `json:"name"`
	Members []UnionMember `json:"members"`
}

// UnionMember represents the declaration of a field in a FIDL2 union.
type UnionMember struct {
	Type Type       `json:"type"`
	Name Identifier `json:"name"`
}

// Struct represents a declaration of a FIDL2 struct.
type Struct struct {
	Name    Identifier     `json:"name"`
	Members []StructMember `json:"members"`
}

// StructMember represents the declaration of a field in a FIDL2 struct.
type StructMember struct {
	Type              Type       `json:"type"`
	Name              Identifier `json:"name"`
	MaybeDefaultValue Constant   `json:"maybe_default_value,omitempty"`
}

// Interface represents the declaration of a FIDL2 interface.
type Interface struct {
	Name    Identifier `json:"name"`
	Methods []Method   `json:"methods"`
}

// Method represents the declaration of a FIDL2 method.
type Method struct {
	Ordinal     Ordinal     `json:"ordinal"`
	Name        Identifier  `json:"name"`
	HasRequest  bool        `json:"has_request"`
	Request     []Parameter `json:"maybe_request,omitempty"`
	HasResponse bool        `json:"has_response"`
	Response    []Parameter `json:"maybe_response,omitempty"`
}

// Parameter represents a parameter to a FIDL2 method.
type Parameter struct {
	Type Type       `json:"type"`
	Name Identifier `json:"name"`
}

// Enum represents a FIDL2 delcaration of an enum.
type Enum struct {
	Name    Identifier       `json:"name"`
	Type    PrimitiveSubtype `json:"type"`
	Members []EnumMember     `json:"members"`
}

// EnumMember represents a single variant in a FIDL2 enum.
type EnumMember struct {
	Name  Identifier `json:"name"`
	Value Constant   `json:"value"`
}

// Const represents a FIDL2 declaration of a named constant.
type Const struct {
	Name  Identifier `json:"name"`
	Type  Type       `json:"type"`
	Value Constant   `json:"value"`
}

// Root is the top-level object for a FIDL2 library.
// It contains lists of all declarations and dependencies within the library.
type Root struct {
	Consts     []Const     `json:"const_declarations,omitempty"`
	Enums      []Enum      `json:"enum_declarations,omitempty"`
	Interfaces []Interface `json:"interface_declarations,omitempty"`
	Structs    []Struct    `json:"struct_declarations,omitempty"`
	Unions     []Union     `json:"union_declarations,omitempty"`
	Libraries  []Library   `json:"library_dependencies,omitempty"`
}
