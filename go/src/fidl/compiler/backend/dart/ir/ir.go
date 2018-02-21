// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strconv"
	"strings"
)

type encodingType string

const (
	primitiveEncodingType   encodingType = "primitive"
	encodableEncodingType                = "encodable"
	stringEncodingType                   = "string"
	handleEncodingType                   = "handle"
	vectorEncodingType                   = "vector"
	typedVectorEncodingType              = "typedVector"
	arrayEncodingType                    = "array"
	typedArrayEncodingType               = "typedArray"
)

type Type struct {
	Decl          string
	ElementCount  string
	Nullable      bool
	typedDataDecl string
	baseDecl      string
	encodingType  encodingType
	codecSuffix   string
}

func (t *Type) Encode(identifer string, offset int) string {
	offsetStr := strconv.Itoa(offset)
	nullableStr := strconv.FormatBool(t.Nullable)
	switch t.encodingType {
	case primitiveEncodingType:
		return fmt.Sprintf("$encoder.encode%s(%s, $offset + %s)", t.codecSuffix, identifer, offsetStr)
	case encodableEncodingType:
		return fmt.Sprintf("$encoder.encodeEncodable(%s, %s.$encodedSize, $offset + %s, %s)", identifer, t.baseDecl, offsetStr, nullableStr)
	case stringEncodingType:
		return fmt.Sprintf("$encoder.encodeString(%s, %s, $offset + %s, %s)", identifer, t.ElementCount, offsetStr, nullableStr)
	case handleEncodingType:
		return fmt.Sprintf("$encoder.encodeHandle(%s, $offset + %s, %s)", identifer, offsetStr, nullableStr)
	case vectorEncodingType:
		return fmt.Sprintf("$encoder.encodeEncodableVector(%s, %s.$encodedSize, %s, $offset + %s, %s)", identifer, t.baseDecl, t.ElementCount, offsetStr, nullableStr)
	case typedVectorEncodingType:
		return fmt.Sprintf("$encoder.encode%sAsVector(%s, %s, $offset + %s, %s)", t.typedDataDecl, identifer, t.ElementCount, offsetStr, nullableStr)
	case arrayEncodingType:
		return fmt.Sprintf("$encoder.encodeEncodableArray(.%s, %s.$encodedSize, %s, $offset + %s)", identifer, t.baseDecl, t.ElementCount, offsetStr)
	case typedArrayEncodingType:
		return fmt.Sprintf("$encoder.encode%sAsArray(%s, %s, $offset + %s)", t.typedDataDecl, identifer, t.ElementCount, offsetStr)
	default:
		log.Fatal("Unknown encodingType:", t.encodingType)
		return ""
	}
}

func (t *Type) Decode(offset int) string {
	offsetStr := strconv.Itoa(offset)
	nullableStr := strconv.FormatBool(t.Nullable)
	switch t.encodingType {
	case primitiveEncodingType:
		return fmt.Sprintf("$decoder.decode%s($offset + %s)", t.codecSuffix, offsetStr)
	case encodableEncodingType:
		return fmt.Sprintf("$decoder.decodeEncodable(%s.$decode, %s.$encodedSize, $offset + %s, %s)", t.baseDecl, t.baseDecl, offsetStr, nullableStr)
	case stringEncodingType:
		return fmt.Sprintf("$decoder.decodeString(%s, $offset + %s, %s)", t.ElementCount, offsetStr, nullableStr)
	case handleEncodingType:
		return fmt.Sprintf("$decoder.decodeHandle($offset + %s, %s)", offsetStr, nullableStr)
	case vectorEncodingType:
		return fmt.Sprintf("$decoder.decodeEncodableVector(%s.$decode, %s.$encodedSize, %s, $offset + %s, %s)", t.baseDecl, t.baseDecl, t.ElementCount, offsetStr, nullableStr)
	case typedVectorEncodingType:
		return fmt.Sprintf("$decoder.decodeVectorAs%s(%s, $offset + %s, %s)", t.typedDataDecl, t.ElementCount, offsetStr, nullableStr)
	case arrayEncodingType:
		return fmt.Sprintf("$decoder.decodeEncodableArray(%s.$decode, %s.$encodedSize, %s, $offset + %s)", t.baseDecl, t.baseDecl, t.ElementCount, offsetStr)
	case typedArrayEncodingType:
		return fmt.Sprintf("$decoder.decodeArrayAs%s(%s, $offset + %s)", t.typedDataDecl, t.ElementCount, offsetStr)
	default:
		log.Fatal("Unknown encodingType:", t.encodingType)
		return ""
	}
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

type Union struct {
	Name        string
	TagName     string
	Members     []UnionMember
	EncodedSize int
}

type UnionMember struct {
	Type   Type
	Name   string
	Offset int
}

type Struct struct {
	Name        string
	Members     []StructMember
	EncodedSize int
}

type StructMember struct {
	Type   Type
	Name   string
	Offset int
}

type Interface struct {
	Name        string
	ProxyName   string
	BindingName string
	Methods     []Method
}

type Method struct {
	Ordinal      types.Ordinal
	OrdinalName  string
	Name         string
	HasRequest   bool
	Request      []Parameter
	RequestSize  int
	HasResponse  bool
	Response     []Parameter
	ResponseSize int
}

type Parameter struct {
	Type   Type
	Name   string
	Offset int
}

type Root struct {
	Enums      []Enum
	Interfaces []Interface
	Structs    []Struct
	Unions     []Union
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

func changeIfReserved(val types.Identifier) string {
	str := string(val)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

type compiler struct {
	declarations *types.DeclarationMap
}

func (c *compiler) compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for _, v := range val {
		strs = append(strs, changeIfReserved(v))
	}
	return strings.Join(strs, ".")
}

func (c *compiler) compileLiteral(val types.Literal) string {
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
}

func (c *compiler) compileConstant(val types.Constant) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileCompoundIdentifier(val.Identifier)
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type:", val)
	return ""
}

func (c *compiler) maybeCompileConstant(val *types.Constant) string {
	if val == nil {
		return "null"
	}
	return c.compileConstant(*val)
}

func (c *compiler) compileType(val types.Type) Type {
	r := Type{}
	r.Nullable = val.Nullable
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
			r.encodingType = typedArrayEncodingType
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
			r.encodingType = arrayEncodingType
		}
		r.ElementCount = c.compileConstant(*val.ElementCount)
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
			r.encodingType = typedVectorEncodingType
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
			r.encodingType = vectorEncodingType
		}
		r.ElementCount = c.maybeCompileConstant(val.ElementCount)
	case types.StringType:
		r.Decl = "String"
		r.encodingType = stringEncodingType
		r.ElementCount = c.maybeCompileConstant(val.ElementCount)
	case types.HandleType:
		r.Decl = "Handle"
		r.encodingType = handleEncodingType
	case types.RequestType:
		t := c.compileCompoundIdentifier(val.RequestSubtype)
		r.Decl = fmt.Sprintf("InterfaceRequest<%s>", t)
		r.baseDecl = "InterfaceRequest"
		r.encodingType = encodableEncodingType
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.typedDataDecl = typedDataDecl[val.PrimitiveSubtype]
		r.encodingType = primitiveEncodingType
		r.codecSuffix = primitiveCodecSuffix[val.PrimitiveSubtype]
	case types.IdentifierType:
		t := c.compileCompoundIdentifier(val.Identifier)
		// TODO(abarth): Need to distguish between interfaces and structs.
		r.Decl = fmt.Sprintf("InterfaceHandle<%s>", t)
		r.baseDecl = "InterfaceHandle"
		r.encodingType = encodableEncodingType
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}
	return r
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	e := Enum{
		changeIfReserved(val.Name),
		[]EnumMember{},
		primitiveEncodedSize[val.Type],
		primitiveCodecSuffix[val.Type],
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			changeIfReserved(v.Name),
			c.compileConstant(v.Value),
		})
	}
	return e
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			c.compileType(v.Type),
			changeIfReserved(v.Name),
			v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		changeIfReserved(val.Name),
		changeIfReserved(val.Name + "Proxy"),
		changeIfReserved(val.Name + "Binding"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := changeIfReserved(v.Name)
		m := Method{
			v.Ordinal,
			fmt.Sprintf("_k%s_%s_Ordinal", r.Name, v.Name),
			name,
			v.HasRequest,
			c.compileParameterArray(v.Request),
			v.RequestSize,
			v.HasResponse,
			c.compileParameterArray(v.Response),
			v.ResponseSize,
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	return StructMember{
		c.compileType(val.Type),
		changeIfReserved(val.Name),
		val.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	r := Struct{
		changeIfReserved(val.Name),
		[]StructMember{},
		val.Size,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		c.compileType(val.Type),
		changeIfReserved(val.Name),
		val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		changeIfReserved(val.Name),
		changeIfReserved(val.Name + "Tag"),
		[]UnionMember{},
		val.Size,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func Compile(fidlData types.Root) Root {
	root := Root{}
	c := compiler{&fidlData.Declarations}

	for _, v := range fidlData.Enums {
		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range fidlData.Interfaces {
		root.Interfaces = append(root.Interfaces, c.compileInterface(v))
	}

	for _, v := range fidlData.Structs {
		root.Structs = append(root.Structs, c.compileStruct(v))
	}

	for _, v := range fidlData.Unions {
		root.Unions = append(root.Unions, c.compileUnion(v))
	}

	return root
}
