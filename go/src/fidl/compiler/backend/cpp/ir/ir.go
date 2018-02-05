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
	prefix string
	suffix string
}

func (t *Type) Decorate(identifer string) string {
	return t.prefix + " " + identifer + t.suffix
}

type Enum struct {
	Name    string
	Type    string
	Members []EnumMember
}

type EnumMember struct {
	Name  string
	Value string
}

type Union struct {
	Name    string
	Members []UnionMember
}

type UnionMember struct {
	Type Type
	Name string
}

type Struct struct {
	Name    string
	CName   string
	Members []StructMember
}

type StructMember struct {
	Type        Type
	Name        string
	StorageName string
}

type Interface struct {
	Name      string
	ProxyName string
	StubName  string
	Methods   []Method
}

type Method struct {
	Ordinal      types.Ordinal
	OrdinalName  string
	Name         string
	HasRequest   bool
	Request      []Parameter
	HasResponse  bool
	Response     []Parameter
	CallbackType string
}

type Parameter struct {
	Type Type
	Name string
}

type Root struct {
	PrimaryHeader string
	Enums         []Enum
	Interfaces    []Interface
	Structs       []Struct
	Unions        []Union
}

var reservedWords = map[string]bool{
	"alignas":          true,
	"alignof":          true,
	"and":              true,
	"and_eq":           true,
	"asm":              true,
	"atomic_cancel":    true,
	"atomic_commit":    true,
	"atomic_noexcept":  true,
	"auto":             true,
	"bitand":           true,
	"bitor":            true,
	"bool":             true,
	"break":            true,
	"case":             true,
	"catch":            true,
	"char":             true,
	"char16_t":         true,
	"char32_t":         true,
	"class":            true,
	"compl":            true,
	"concept":          true,
	"const":            true,
	"constexpr":        true,
	"const_cast":       true,
	"continue":         true,
	"co_await":         true,
	"co_return":        true,
	"co_yield":         true,
	"decltype":         true,
	"default":          true,
	"delete":           true,
	"do":               true,
	"double":           true,
	"dynamic_cast":     true,
	"else":             true,
	"enum":             true,
	"explicit":         true,
	"export":           true,
	"extern":           true,
	"false":            true,
	"float":            true,
	"for":              true,
	"friend":           true,
	"goto":             true,
	"if":               true,
	"import":           true,
	"inline":           true,
	"int":              true,
	"long":             true,
	"module":           true,
	"mutable":          true,
	"namespace":        true,
	"new":              true,
	"noexcept":         true,
	"not":              true,
	"not_eq":           true,
	"nullptr":          true,
	"operator":         true,
	"or":               true,
	"or_eq":            true,
	"private":          true,
	"protected":        true,
	"public":           true,
	"register":         true,
	"reinterpret_cast": true,
	"requires":         true,
	"return":           true,
	"short":            true,
	"signed":           true,
	"sizeof":           true,
	"static":           true,
	"static_assert":    true,
	"static_cast":      true,
	"struct":           true,
	"switch":           true,
	"synchronized":     true,
	"template":         true,
	"this":             true,
	"thread_local":     true,
	"throw":            true,
	"true":             true,
	"try":              true,
	"typedef":          true,
	"typeid":           true,
	"typename":         true,
	"union":            true,
	"unsigned":         true,
	"using":            true,
	"virtual":          true,
	"void":             true,
	"volatile":         true,
	"wchar_t":          true,
	"while":            true,
	"xor":              true,
	"xor_eq":           true,
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "zx_status_t",
	types.Int8:    "int8_t",
	types.Int16:   "int16_t",
	types.Int32:   "int32_t",
	types.Int64:   "int64_t",
	types.Uint8:   "uint8_t",
	types.Uint16:  "uint16_t",
	types.Uint32:  "uint32_t",
	types.Uint64:  "uint64_t",
	types.Float32: "float",
	types.Float64: "double",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func compileIdentier(val types.Identifier) string {
	str := string(val)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for _, v := range val {
		strs = append(strs, compileIdentier(v))
	}
	return strings.Join(strs, "::")
}

func compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("\"%q\"", val.Value)
	case types.NumericLiteral:
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
	prefix := ""
	suffix := ""
	switch val.Kind {
	case types.ArrayType:
		t := compileType(*val.ElementType)
		prefix = t.prefix
		suffix = fmt.Sprintf("%s[%s]", t.suffix, compileConstant(val.ElementCount))
	case types.VectorType:
		t := compileType(*val.ElementType)
		if len(t.suffix) > 0 {
			log.Fatal("Cannot compile a vector that contains an array:", val)
		}
		prefix = fmt.Sprintf("::fidl::VectorPtr<%s>", t.prefix)
	case types.StringType:
		prefix = "::fidl::StringPtr"
	case types.HandleType:
		prefix = fmt.Sprintf("::zx::%s", val.HandleSubtype)
	case types.RequestType:
		t := compileCompoundIdentifier(val.RequestSubtype)
		prefix = fmt.Sprintf("::fidl::InterfaceRequest<%s>", t)
	case types.PrimitiveType:
		prefix = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := compileCompoundIdentifier(val.Identifier)
		// TODO(abarth): Need to distguish between interfaces and structs.
		prefix = fmt.Sprintf("::fidl::InterfaceHandle<%s>", t)
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}
	return Type{prefix, suffix}
}

func compileEnum(val types.Enum) Enum {
	e := Enum{
		compileIdentier(val.Name),
		compilePrimitiveSubtype(val.Type),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			compileIdentier(v.Name),
			compileConstant(v.Value),
		})
	}
	return e
}

func compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			compileType(v.Type),
			compileIdentier(v.Name),
		}
		r = append(r, p)
	}

	return r
}

func compileInterface(val types.Interface) Interface {
	r := Interface{
		compileIdentier(val.Name),
		compileIdentier(val.Name + "Proxy"),
		compileIdentier(val.Name + "Stub"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := compileIdentier(v.Name)
		callbackType := ""
		if v.HasResponse {
			callbackType = compileIdentier(v.Name + "Callback")
		}
		m := Method{
			v.Ordinal,
			fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
			name,
			v.HasRequest,
			compileParameterArray(v.Request),
			v.HasResponse,
			compileParameterArray(v.Response),
			callbackType,
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func compileStructMember(val types.StructMember) StructMember {
	return StructMember{
		compileType(val.Type),
		compileIdentier(val.Name + "Ptr"),
		compileIdentier(val.Name + "_"),
	}
}

func compileStruct(val types.Struct) Struct {
	name := compileIdentier(val.Name)
	r := Struct{
		name,
		"::" + name,
		[]StructMember{},
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, compileStructMember(v))
	}

	return r
}

func compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		compileType(val.Type),
		compileIdentier(val.Name),
	}
}

func compileUnion(val types.Union) Union {
	r := Union{
		compileIdentier(val.Name),
		[]UnionMember{},
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, compileUnionMember(v))
	}

	return r
}

func Compile(fidlData types.Root) Root {
	root := Root{}

	// TODO(abarth): Constants.

	for _, v := range fidlData.Enums {
		root.Enums = append(root.Enums, compileEnum(v))
	}

	for _, v := range fidlData.Interfaces {
		root.Interfaces = append(root.Interfaces, compileInterface(v))
	}

	for _, v := range fidlData.Structs {
		root.Structs = append(root.Structs, compileStruct(v))
	}

	for _, v := range fidlData.Unions {
		root.Unions = append(root.Unions, compileUnion(v))
	}

	return root
}
