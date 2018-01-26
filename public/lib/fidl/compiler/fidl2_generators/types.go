// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

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

// SizeNum represents the size of a piece of data in number of bytes.
type SizeNum uint32

// NumericLiteral is a text-based representation of a FIDL2 numeric literal.
type NumericLiteral string

// TODO(cramertj/kulakowski): support multipart names
// NameT represents a FIDL2 identifier.
type NameT string

// TypeT represents the name of a FIDL2 type.
type TypeT string

// EnumType is an enum describing the numeric representation of a FIDL2 enum.
type EnumType string

const (
	Enum8   EnumType = "int8"
	Enum16           = "int16"
	Enum32           = "int32"
	Enum64           = "int64"
	EnumU8           = "uint8"
	EnumU16          = "uint16"
	EnumU32          = "uint32"
	EnumU64          = "uint64"
)

// Literal is a FIDL2 literal such as a string, number, boolean, or default.
type Literal struct {
	IsString  bool           `json:"is_string,omitempty"`
	String    string         `json:"maybe_string,omitempty"`
	IsNumeric bool           `json:"is_numeric,omitempty"`
	Numeric   NumericLiteral `json:"maybe_numeric,omitempty"`
	IsBoolean bool           `json:"is_boolean,omitempty"`
	Boolean   bool           `json:"maybe_boolean,omitempty"`
	IsDefault bool           `json:"is_default,omitempty"`
}

// Constant represents a FIDL2 constant value.
type Constant struct {
	IsLiteral    bool    `json:"is_literal,omitempty"`
	LiteralValue Literal `json:"maybe_literal,omitempty"`
	IsIdentifier bool    `json:"is_identifier,omitempty"`
	Identifier   NameT   `json:"maybe_identifier,omitempty"`
}

// ConstDeclaration represents a FIDL2 declaration of a named constant.
type ConstDeclaration struct {
	Name  NameT    `json:"name,omitempty"`
	Type  TypeT    `json:"type,omitempty"`
	Value Constant `json:"value,omitempty"`
}

// EnumDeclaration represents a FIDL2 delcaration of an enum.
type EnumDeclaration struct {
	Name           NameT       `json:"name,omitempty"`
	UnderlyingType EnumType    `json:"underlying-type,omitempty"`
	Values         []EnumValue `json:"values,omitempty"`
}

// EnumValue represents a single variant in a FIDL2 enum.
type EnumValue struct {
	Name  NameT    `json:"name,omitempty"`
	Value Constant `json:"value,omitempty"`
}

// InterfaceDeclaration represents the declaration of a FIDL2
// interface.
type InterfaceDeclaration struct {
	Name    NameT               `json:"name,omitempty"`
	Methods []MethodDeclaration `json:"methods,omitempty"`
}

// MethodDeclaration represents the declaration of a FIDL2 method.
type MethodDeclaration struct {
	Name        NameT       `json:"name,omitempty"`
	Ordinal     SizeNum     `json:"ordinal,omitempty"`
	HasRequest  bool        `json:"has_request,omitempty"`
	Request     []Parameter `json:"maybe_request,omitempty"`
	HasResponse bool        `json:"has_response,omitempty"`
	Response    []Parameter `json:"maybe_response,omitempty"`
}

// Parameter represents a parameter to a FIDL2 method.
type Parameter struct {
	Name NameT `json:"name,omitempty"`
	Type TypeT `json:"type,omitempty"`
}

// ModuleDependency represents a FIDL2 dependency on a separate module.
type ModuleDependency struct {
	// TODO(cramertj/kulakowski)
}

// Root is the top-level object for a FIDL2 module.
// It contains lists of all declarations and dependencies within the module.
type Root struct {
	ConstDeclarations     []ConstDeclaration     `json:"const-declarations,omitempty"`
	EnumDeclarations      []EnumDeclaration      `json:"enum-declarations,omitempty"`
	InterfaceDeclarations []InterfaceDeclaration `json:"interface-declarations,omitempty"`
	ModuleDependencies    []ModuleDependency     `json:"module-dependencies,omitempty"`
	StructDeclarations    []StructDeclaration    `json:"struct-declarations,omitempty"`
	UnionDeclarations     []UnionDeclaration     `json:"union-declarations,omitempty"`
}

// StructDeclaration represents a declaration of a FIDL2 struct.
type StructDeclaration struct {
	Name   NameT         `json:"name,omitempty"`
	Fields []StructField `json:"fields,omitempty"`
	Size   SizeNum       `json:"size,omitempty"`
}

// StructField represents the declaration of a field in a FIDL2 struct.
type StructField struct {
	Name   NameT   `json:"name,omitempty"`
	Offset SizeNum `json:"offset,omitempty"`
	Type   TypeT   `json:"type,omitempty"`
}

// UnionDeclaration represents the declaration of a FIDL2 union.
type UnionDeclaration struct {
	Name   NameT        `json:"name"`
	Fields []UnionField `json:"fields,omitempty"`
	Size   SizeNum      `json:"size,omitempty"`
}

// UnionField represents the declaration of a field in a FIDL2 union.
type UnionField struct {
	Name NameT `json:"name,omitempty"`
	Type TypeT `json:"type,omitempty"`
}
