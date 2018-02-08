// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strings"
)

type Type struct {
	Decl          string
	TypedDataDecl string
	ElementCount  string
}

type Enum struct {
	Name        string
	Members     []EnumMember
	EncodedSize int
	CodecSuffix string
}

type EnumMember struct {
	Name  string
	Value string
}

type Struct struct {
	Name    string
	Members []StructMember
}

type StructMember struct {
	Type Type
	Name string
}

type Root struct {
	Enums []Enum
}

var reservedWords = map[string]bool{
	"abstract":   true,
	"as":         true,
	"assert":     true,
	"async":      true,
	"await":      true,
	"break":      true,
	"case":       true,
	"catch":      true,
	"class":      true,
	"const":      true,
	"continue":   true,
	"covariant":  true,
	"default":    true,
	"deferred":   true,
	"do":         true,
	"dynamic":    true,
	"else":       true,
	"enum":       true,
	"export":     true,
	"extends":    true,
	"external":   true,
	"factory":    true,
	"false":      true,
	"final":      true,
	"finally":    true,
	"for":        true,
	"get":        true,
	"if":         true,
	"implements": true,
	"import":     true,
	"in":         true,
	"is":         true,
	"library":    true,
	"new":        true,
	"null":       true,
	"operator":   true,
	"part":       true,
	"rethrow":    true,
	"return":     true,
	"set":        true,
	"static":     true,
	"super":      true,
	"switch":     true,
	"this":       true,
	"throw":      true,
	"true":       true,
	"try":        true,
	"typedef":    true,
	"var":        true,
	"void":       true,
	"while":      true,
	"with":       true,
	"yield":      true,
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "int",
	types.Int8:    "int",
	types.Int16:   "int",
	types.Int32:   "int",
	types.Int64:   "int",
	types.Uint8:   "int",
	types.Uint16:  "int",
	types.Uint32:  "int",
	types.Uint64:  "int",
	types.Float32: "double",
	types.Float64: "double",
}

var typedDataDecl = map[types.PrimitiveSubtype]string{
	types.Bool:    "Uint8List",
	types.Status:  "Int32List",
	types.Int8:    "Int8List",
	types.Int16:   "Int16List",
	types.Int32:   "Int32List",
	types.Int64:   "Int64List",
	types.Uint8:   "Uint8List",
	types.Uint16:  "Uint16List",
	types.Uint32:  "Uint32List",
	types.Uint64:  "Uint64List",
	types.Float32: "Float32List",
	types.Float64: "Float64List",
}

var primitiveEncodedSize = map[types.PrimitiveSubtype]int{
	types.Bool:    1,
	types.Status:  4,
	types.Int8:    1,
	types.Int16:   2,
	types.Int32:   4,
	types.Int64:   8,
	types.Uint8:   1,
	types.Uint16:  2,
	types.Uint32:  4,
	types.Uint64:  8,
	types.Float32: 4,
	types.Float64: 8,
}

var primitiveCodecSuffix = map[types.PrimitiveSubtype]string{
	types.Bool:    "Bool",
	types.Status:  "Int32",
	types.Int8:    "Int8",
	types.Int16:   "Int16",
	types.Int32:   "Int32",
	types.Int64:   "Int64",
	types.Uint8:   "Uint8",
	types.Uint16:  "Uint16",
	types.Uint32:  "Uint32",
	types.Uint64:  "Uint64",
	types.Float32: "Float32",
	types.Float64: "Float64",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func compileIdentifier(val types.Identifier) string {
	str := string(val)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for _, v := range val {
		strs = append(strs, compileIdentifier(v))
	}
	return strings.Join(strs, ".")
}

func compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("\"%q\"", val.Value)
	case types.NumericLiteral:
		// TODO(abarth): Values larger than max int64 need to be encoded in hex.
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.DefaultLiteral:
		return "default"
	default:
		log.Fatal("Unknown literal kind:", val.Kind)
		return ""
	}
	log.Fatal("Unknown literal:", val)
	return ""
}

func compileConstant(val types.Constant) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return compileCompoundIdentifier(val.Identifier)
	case types.LiteralConstant:
		return compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind:", val.Kind)
		return ""
	}
}

func compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type:", val)
	return ""
}

func compileType(val types.Type) Type {
	r := Type{}
	switch val.Kind {
	case types.ArrayType:
		fallthrough
	case types.VectorType:
		t := compileType(*val.ElementType)
		if len(t.TypedDataDecl) > 0 {
			r.Decl = t.TypedDataDecl
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
		}
		r.ElementCount = compileConstant(val.ElementCount)
	case types.StringType:
		r.Decl = "String"
	case types.HandleType:
		r.Decl = "Handle"
	case types.RequestType:
		t := compileCompoundIdentifier(val.RequestSubtype)
		r.Decl = fmt.Sprintf("InterfaceRequest<%s>", t)
	case types.PrimitiveType:
		r.Decl = compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.TypedDataDecl = typedDataDecl[val.PrimitiveSubtype]
	case types.IdentifierType:
		t := compileCompoundIdentifier(val.Identifier)
		// TODO(abarth): Need to distguish between interfaces and structs.
		r.Decl = fmt.Sprintf("InterfaceHandle<%s>", t)
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}
	return r
}

func compileEnum(val types.Enum) Enum {
	e := Enum{
		compileIdentifier(val.Name),
		[]EnumMember{},
		primitiveEncodedSize[val.Type],
		primitiveCodecSuffix[val.Type],
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			compileIdentifier(v.Name),
			compileConstant(v.Value),
		})
	}
	return e
}

func Compile(fidlData types.Root) Root {
	root := Root{}

	for _, v := range fidlData.Enums {
		root.Enums = append(root.Enums, compileEnum(v))
	}

	return root
}
