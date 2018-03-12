// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"strconv"
	"strings"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
)

// Type represents a golang type.
type Type string

// Enum represents the idiomatic representation of an enum in golang.
//
// That is, something like:
// type MyEnum int32
// const (
//    MyEnumMember1 MyEnum = 1
//    MyEnumMember2        = 2
//    ...
// )
type Enum struct {
	// Name is the name of the enum type alias.
	Name string

	// Type is the underlying primitive type for the enum.
	Type Type

	// Members is the list of enum variants that are a part of this enum.
	// The values of the Members must not overlap.
	Members []EnumMember
}

// EnumMember represents a single enum variant. See Enum for more details.
type EnumMember struct {
	// Name is the name of the enum variant without any prefix.
	Name string

	// Value is the raw value of the enum variant, represented as a string
	// to support many types.
	Value string
}

// Struct represents a golang struct.
type Struct struct {
	// Name is the name of the golang struct.
	Name string

	// Members is a list of the golang struct members.
	Members []StructMember

	// Size is the FIDL-encoded size of the struct.
	Size int

	// Alignment is the alignment of the FIDL-encoded struct.
	Alignment int
}

// Tag loosely represents a golang struct member tag for maximum elements e.g.
// `fidl:"3,4,5"`. For a type like vector<vector<int>:3>:4, the tag would
// look like `fidl:"3,4"`, such that the commas separate nesting. Note that if
// a nested type that doesn't specify a max size is used, it is encoded as the
// empty string. For example, vector<vector<vector<int>:3>>:5 would have tag
// `fidl:"3,,5"`. Note that this makes a maximum length of 0 distinct.
// Fundamentally, this Tag exists to provide type metadata for nested FIDL2
// container types to the encoder/decoder in the bindings.
//
// Note that arrays are not included because their maximum sizes are encoded in
// the golang type system. Because of this, FIDL types such as
// vector<array<int>:10>:10 will have a tag that looks like `fidl:"10"`.
type Tag struct {
	// MaxElems is the maximum number of elements a type is annotated with.
	MaxElems []*int
}

// String generates a string representation for the tag.
func (t *Tag) String() string {
	var elemsTag []string
	if len(t.MaxElems) == 0 {
		return ""
	}
	anyNonNil := false
	for _, elems := range t.MaxElems {
		if elems == nil {
			elemsTag = append(elemsTag, "")
			continue
		}
		anyNonNil = true
		elemsTag = append(elemsTag, strconv.Itoa(*elems))
	}
	if !anyNonNil {
		return ""
	}
	return fmt.Sprintf("`fidl:\"%s\"`", strings.Join(elemsTag, ","))
}

// StructMember represents the member of a golang struct.
type StructMember struct {
	// Name is the name of the golang struct member.
	Name string

	// Type is the type of the golang struct member.
	Type Type

	// Tag is the golang struct member tag which holds additional metadata
	// about the struct field.
	Tag string
}

// Root is the root of the golang backend IR structure.
//
// The golang backend IR structure is loosely modeled after an abstract syntax
// tree, and is used to generate golang code from templates.
type Root struct {
	// TODO(mknyszek): Support unions, interfaces, and constants.

	// Enums represents a list of FIDL enums represented as Go enums.
	Enums []Enum

	// Structs represents the list of FIDL structs represented as Go structs.
	Structs []Struct
}

// compiler contains the state necessary for recursive compilation.
type compiler struct {
	// decls contains all top-level declarations for the FIDL source.
	decls types.DeclMap
}

// Contains the full set of reserved golang keywords, in addition to a set of
// primitive named types. Note that this will result in potentially unnecessary
// identifier renaming, but this isn't a big deal for generated code.
var reservedWords = map[string]bool{
	// Officially reserved keywords.
	"break":       true,
	"case":        true,
	"chan":        true,
	"const":       true,
	"continue":    true,
	"default":     true,
	"defer":       true,
	"else":        true,
	"fallthrough": true,
	"for":         true,
	"func":        true,
	"go":          true,
	"goto":        true,
	"if":          true,
	"int":         true,
	"interface":   true,
	"map":         true,
	"package":     true,
	"range":       true,
	"return":      true,
	"select":      true,
	"struct":      true,
	"switch":      true,
	"try":         true,
	"type":        true,
	"var":         true,

	// Reserved types.
	"bool":   true,
	"byte":   true,
	"int8":   true,
	"int16":  true,
	"int32":  true,
	"int64":  true,
	"rune":   true,
	"string": true,
	"uint8":  true,
	"uint16": true,
	"uint32": true,
	"uint64": true,

	// Reserved values.
	"false": true,
	"true":  true,
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "int32",
	types.Int8:    "int8",
	types.Int16:   "int16",
	types.Int32:   "int32",
	types.Int64:   "int64",
	types.Uint8:   "uint8",
	types.Uint16:  "uint16",
	types.Uint32:  "uint32",
	types.Uint64:  "uint64",
	types.Float32: "float32",
	types.Float64: "float64",
}

func exportIdentifier(name types.Identifier) types.Identifier {
	return types.Identifier(common.ToCamelCase(string(name)))
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(val types.Identifier) string {
	// TODO(mknyszek): Detect name collision within a scope as a result of transforming.
	str := string(val)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func (_ *compiler) compileLiteral(val types.Literal) string {
	switch val.Kind {
	// TODO(mknyszek): Support string and default literals.
	case types.NumericLiteral:
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	default:
		log.Fatal("Unknown literal kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant) string {
	switch val.Kind {
	// TODO(mknyszek): Support identifiers.
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind: ", val.Kind)
		return ""
	}
}

func (_ *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) Type {
	t, ok := primitiveTypes[val]
	if !ok {
		log.Fatal("Unknown primitive type: ", val)
	}
	return Type(t)
}

func (c *compiler) compileType(val types.Type) (r Type, t Tag) {
	// TODO(mknyszek): Support handles, requests and identifiers.
	switch val.Kind {
	case types.ArrayType:
		e, et := c.compileType(*val.ElementType)
		r = Type(fmt.Sprintf("[%s]%s", strconv.Itoa(*val.ElementCount), e))
		t = et
	case types.StringType:
		t.MaxElems = append(t.MaxElems, val.ElementCount)
		if val.Nullable {
			r = Type("*string")
		} else {
			r = Type("string")
		}
	case types.VectorType:
		e, et := c.compileType(*val.ElementType)
		et.MaxElems = append(et.MaxElems, val.ElementCount)
		if val.Nullable {
			r = Type(fmt.Sprintf("*[]%s", e))
		} else {
			r = Type(fmt.Sprintf("[]%s", e))
		}
		t = et
	case types.PrimitiveType:
		r = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		// val.Identifier is a CompoundIdentifier, but we don't have the
		// ability to look up the declaration type for identifiers in other
		// libraries. We also don't know how cross-library identifier references
		// are even going to look for Go. For now, just use the first component
		// of the identifier and assume that the identifier is in this library.
		e := changeIfReserved(exportIdentifier(val.Identifier[0]))
		declType, ok := c.decls[val.Identifier[0]]
		if !ok {
			log.Fatal("Unknown identifier:", val.Identifier)
		}
		// TODO(mknyszek): Support unions and interfaces.
		switch declType {
		case types.EnumDeclType:
			r = Type(e)
		case types.StructDeclType:
			if val.Nullable {
				r = Type("*" + e)
			} else {
				r = Type(e)
			}
		default:
			log.Fatal("Unknown declaration type:", declType)
		}
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}
	return
}

func (c *compiler) compileEnumMember(val types.EnumMember) EnumMember {
	return EnumMember{
		Name:  changeIfReserved(exportIdentifier(val.Name)),
		Value: c.compileConstant(val.Value),
	}
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	r := Enum{
		Name: changeIfReserved(exportIdentifier(val.Name[0])),
		Type: c.compilePrimitiveSubtype(val.Type),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileEnumMember(v))
	}
	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	ty, tag := c.compileType(val.Type)
	return StructMember{
		Type: ty,
		Name: changeIfReserved(exportIdentifier(val.Name)),
		Tag:  tag.String(),
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	r := Struct{
		Name:      changeIfReserved(exportIdentifier(val.Name[0])),
		Size:      val.Size,
		Alignment: val.Alignment,
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}
	return r
}

// Compile translates parsed FIDL IR into golang backend IR for code generation.
func Compile(fidlData types.Root) Root {
	c := compiler{decls: fidlData.Decls}
	r := Root{}
	for _, v := range fidlData.Enums {
		r.Enums = append(r.Enums, c.compileEnum(v))
	}
	for _, v := range fidlData.Structs {
		r.Structs = append(r.Structs, c.compileStruct(v))
	}
	return r
}
