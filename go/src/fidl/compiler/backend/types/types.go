// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

import (
	"encoding/json"
	"fmt"
	"strings"
)

/*
This file contains types which describe FIDL interfaces.

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

type LibraryIdentifier []Identifier

type CompoundIdentifier struct {
	Library LibraryIdentifier
	Name    Identifier
}

type EncodedLibraryIdentifier string

type EncodedCompoundIdentifier string

func (eli EncodedLibraryIdentifier) Parts() LibraryIdentifier {
	return ParseLibraryName(eli)
}

func (eli EncodedLibraryIdentifier) PartsReversed() []string {
	parts := eli.Parts()
	partsReversed := make([]string, len(parts))
	for i, part := range parts {
		partsReversed[len(parts)-i-1] = string(part)
	}

	return partsReversed
}

func (eci EncodedCompoundIdentifier) Parts() CompoundIdentifier {
	return ParseCompoundIdentifier(eci)
}

func (eci EncodedCompoundIdentifier) LibraryName() EncodedLibraryIdentifier {
	parts := strings.SplitN(string(eci), "/", 2)
	raw_library := ""
	if len(parts) == 2 {
		raw_library = parts[0]
	}
	return EncodedLibraryIdentifier(raw_library)
}

func ParseLibraryName(eli EncodedLibraryIdentifier) LibraryIdentifier {
	raw_parts := strings.Split(string(eli), ".")
	parts := make([]Identifier, len(raw_parts))
	for i, raw_part := range raw_parts {
		parts[i] = Identifier(raw_part)
	}
	return LibraryIdentifier(parts)
}

func ParseCompoundIdentifier(eci EncodedCompoundIdentifier) CompoundIdentifier {
	parts := strings.SplitN(string(eci), "/", 2)
	raw_library := ""
	raw_name := parts[0]
	if len(parts) == 2 {
		raw_library = parts[0]
		raw_name = parts[1]
	}
	library := ParseLibraryName(EncodedLibraryIdentifier(raw_library))
	name := Identifier(raw_name)
	return CompoundIdentifier{library, name}
}

func EnsureLibrary(l EncodedLibraryIdentifier, eci EncodedCompoundIdentifier) EncodedCompoundIdentifier {
	if strings.Index(string(eci), "/") != -1 {
		return eci
	}
	new_eci := strings.Join([]string{string(l), "/", string(eci)}, "")
	return EncodedCompoundIdentifier(new_eci)
}

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
	Handle    HandleSubtype = "handle"
	Process                 = "process"
	Thread                  = "thread"
	Vmo                     = "vmo"
	Channel                 = "channel"
	Event                   = "event"
	Port                    = "port"
	Interrupt               = "interrupt"
	Log                     = "log"
	Socket                  = "socket"
	Resource                = "resource"
	Eventpair               = "eventpair"
	Job                     = "job"
	Vmar                    = "vmar"
	Fifo                    = "fifo"
	Guest                   = "guest"
	Time                    = "timer"
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
	Kind       ConstantKind              `json:"kind"`
	Identifier EncodedCompoundIdentifier `json:"identifier,omitempty"`
	Literal    Literal                   `json:"literal,omitempty"`
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
	ElementCount     *int
	HandleSubtype    HandleSubtype
	RequestSubtype   EncodedCompoundIdentifier
	PrimitiveSubtype PrimitiveSubtype
	Identifier       EncodedCompoundIdentifier
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
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
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
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case HandleType:
		err = json.Unmarshal(*obj["subtype"], &t.HandleSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case RequestType:
		err = json.Unmarshal(*obj["subtype"], &t.RequestSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
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
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	default:
		return fmt.Errorf("Unknown type kind: %s", t.Kind)
	}

	return nil
}

type Attribute struct {
	Name  Identifier `json:"name"`
	Value string     `json:"value"`
}

// Union represents the declaration of a FIDL union.
type Union struct {
	Attributes []Attribute               `json:"maybe_attributes,omitempty"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Members    []UnionMember             `json:"members"`
	Size       int                       `json:"size"`
	Alignment  int                       `json:"alignment"`
}

func (d *Union) GetAttribute(name Identifier) string {
	for _, a := range d.Attributes {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

// UnionMember represents the declaration of a field in a FIDL union.
type UnionMember struct {
	Type   Type       `json:"type"`
	Name   Identifier `json:"name"`
	Offset int        `json:"offset"`
}

// Struct represents a declaration of a FIDL struct.
type Struct struct {
	Attributes []Attribute               `json:"maybe_attributes,omitempty"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Members    []StructMember            `json:"members"`
	Size       int                       `json:"size"`
	Alignment  int                       `json:"alignment"`
}

func (d *Struct) GetAttribute(name Identifier) string {
	for _, a := range d.Attributes {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

// StructMember represents the declaration of a field in a FIDL struct.
type StructMember struct {
	Type              Type       `json:"type"`
	Name              Identifier `json:"name"`
	Offset            int        `json:"offset"`
	MaybeDefaultValue *Constant  `json:"maybe_default_value,omitempty"`
}

// Interface represents the declaration of a FIDL interface.
type Interface struct {
	Attributes []Attribute               `json:"maybe_attributes,omitempty"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Methods    []Method                  `json:"methods"`
}

func (d *Interface) HasAttribute(name Identifier) bool {
	for _, a := range d.Attributes {
		if a.Name == name {
			return true
		}
	}
	return false
}

func (d *Interface) GetAttribute(name Identifier) string {
	for _, a := range d.Attributes {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

func (d *Interface) GetServiceName() string {
	if d.HasAttribute("Discoverable") {
		ci := ParseCompoundIdentifier(d.Name)
		var parts []string
		for _, i := range ci.Library {
			parts = append(parts, string(i))
		}
		parts = append(parts, string(ci.Name))
		return "\"" + strings.Join(parts, ".") + "\""
	}
	return ""
}

// Method represents the declaration of a FIDL method.
type Method struct {
	Ordinal      Ordinal     `json:"ordinal"`
	Name         Identifier  `json:"name"`
	HasRequest   bool        `json:"has_request"`
	Request      []Parameter `json:"maybe_request,omitempty"`
	RequestSize  int         `json:"maybe_request_size,omitempty"`
	HasResponse  bool        `json:"has_response"`
	Response     []Parameter `json:"maybe_response,omitempty"`
	ResponseSize int         `json:"maybe_response_size,omitempty"`
}

// Parameter represents a parameter to a FIDL method.
type Parameter struct {
	Type   Type       `json:"type"`
	Name   Identifier `json:"name"`
	Offset int        `json:"offset"`
}

// Enum represents a FIDL delcaration of an enum.
type Enum struct {
	Attributes []Attribute               `json:"maybe_attributes,omitempty"`
	Type       PrimitiveSubtype          `json:"type"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Members    []EnumMember              `json:"members"`
}

func (d *Enum) GetAttribute(name Identifier) string {
	for _, a := range d.Attributes {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

// EnumMember represents a single variant in a FIDL enum.
type EnumMember struct {
	Name  Identifier `json:"name"`
	Value Constant   `json:"value"`
}

// Const represents a FIDL declaration of a named constant.
type Const struct {
	Attributes []Attribute               `json:"maybe_attributes,omitempty"`
	Type       Type                      `json:"type"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Value      Constant                  `json:"value"`
}

func (d *Const) GetAttribute(name Identifier) string {
	for _, a := range d.Attributes {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

type DeclType string

const (
	ConstDeclType     DeclType = "const"
	EnumDeclType               = "enum"
	InterfaceDeclType          = "interface"
	StructDeclType             = "struct"
	UnionDeclType              = "union"
)

type DeclMap map[EncodedCompoundIdentifier]DeclType

// Library represents a FIDL dependency on a separate library.
type Library struct {
	Name  EncodedLibraryIdentifier `json:"name,omitempty"`
	Decls DeclMap                  `json:"declarations,omitempty"`
}

// Root is the top-level object for a FIDL library.
// It contains lists of all declarations and dependencies within the library.
type Root struct {
	Name       EncodedLibraryIdentifier    `json:"name,omitempty"`
	Consts     []Const                     `json:"const_declarations,omitempty"`
	Enums      []Enum                      `json:"enum_declarations,omitempty"`
	Interfaces []Interface                 `json:"interface_declarations,omitempty"`
	Structs    []Struct                    `json:"struct_declarations,omitempty"`
	Unions     []Union                     `json:"union_declarations,omitempty"`
	DeclOrder  []EncodedCompoundIdentifier `json:"declaration_order,omitempty"`
	Decls      DeclMap                     `json:"declarations,omitempty"`
	Libraries  []Library                   `json:"library_dependencies,omitempty"`
}
