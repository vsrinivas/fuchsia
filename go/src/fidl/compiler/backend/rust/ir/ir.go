// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strings"
)

type Type string

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
	Members []StructMember
}

type StructMember struct {
	Type Type
	Name string
}

type Interface struct {
	Name      string
	ProxyName string
	StubName  string
	Methods   []Method
}

type Method struct {
	Ordinal     types.Ordinal
	OrdinalName string
	Name        string
	HasRequest  bool
	Request     []Parameter
	HasResponse bool
	Response    []Parameter
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
	"as":       true,
	"box":      true,
	"break":    true,
	"const":    true,
	"continue": true,
	"crate":    true,
	"else":     true,
	"enum":     true,
	"extern":   true,
	"false":    true,
	"fn":       true,
	"for":      true,
	"if":       true,
	"impl":     true,
	"in":       true,
	"let":      true,
	"loop":     true,
	"match":    true,
	"mod":      true,
	"move":     true,
	"mut":      true,
	"pub":      true,
	"ref":      true,
	"return":   true,
	"self":     true,
	"Self":     true,
	"static":   true,
	"struct":   true,
	"super":    true,
	"trait":    true,
	"true":     true,
	"type":     true,
	"unsafe":   true,
	"use":      true,
	"where":    true,
	"while":    true,

	// Keywords reserved for future use (future-proofing...)
	"abstract": true,
	"alignof":  true,
	"await":    true,
	"become":   true,
	"do":       true,
	"final":    true,
	"macro":    true,
	"offsetof": true,
	"override": true,
	"priv":     true,
	"proc":     true,
	"pure":     true,
	"sizeof":   true,
	"typeof":   true,
	"unsized":  true,
	"virtual":  true,
	"yield":    true,

	// Weak keywords (special meaning in specific contexts)
	// These are ok in all contexts of fidl names.
	//"default":	true,
	//"union":	true,

	// Things that are not keywords, but for which collisions would be very unpleasant
	"Ok":     true,
	"Err":    true,
	"Vec":    true,
	"Option": true,
	"Some":   true,
	"None":   true,
	"Box":    true,

	// Names used by the FIDL bindings
	"Server":                 true,
	"Client":                 true,
	"Dispatcher":             true,
	"DispatchResponseFuture": true,
	"DispatchFuture":         true,
	"Proxy":                  true,
	"Service":                true,
	"NAME":                   true,
	"VERSION":                true,
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

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "::zx::Status",
	types.Int8:    "i8",
	types.Int16:   "i16",
	types.Int32:   "i32",
	types.Int64:   "i64",
	types.Uint8:   "u8",
	types.Uint16:  "u16",
	types.Uint32:  "u32",
	types.Uint64:  "u64",
	types.Float32: "f32",
	types.Float64: "f64",
}

var handleSubtypes = map[types.HandleSubtype]string{
	types.Handle:     "Handle",
	types.Process:    "Process",
	types.Thread:     "Thread",
	types.Vmo:        "Vmo",
	types.Channel:    "Channel",
	types.Event:      "Event",
	types.Port:       "Port",
	types.Interrupt:  "Interrupt",
	types.Iomap:      "IoMap",
	types.Pci:        "Pci",
	types.Log:        "Log",
	types.Socket:     "Socket",
	types.Resource:   "Resource",
	types.Eventpair:  "EventPair",
	types.Job:        "Job",
	types.Vmar:       "Vmar",
	types.Fifo:       "Fifo",
	types.Hypervisor: "Hypervisor",
	types.Guest:      "Guest",
	types.Time:       "Timer",
}

func compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for i, v := range val {
		str := changeIfReserved(v)
		if i == (len(val) - 1) {
			// The last component should be camel-cased
			str = common.ToCamelCase(str)
		}
		strs = append(strs, str)
	}
	return strings.Join(strs, "::")
}

func compileCamelIdentifier(val types.Identifier) string {
	return common.ToCamelCase(changeIfReserved(val))
}

func compileSnakeIdentifier(val types.Identifier) string {
	return common.ToSnakeCase(changeIfReserved(val))
}

func compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		return fmt.Sprintf("r###\"%q\"###", val.Value)
	case types.NumericLiteral:
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.DefaultLiteral:
		return "::Default::default()"
	default:
		log.Fatal("Unknown literal kind:", val.Kind)
		return ""
	}
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

func compileHandleSubtype(val types.HandleSubtype) string {
	if t, ok := handleSubtypes[val]; ok {
		return t
	}
	log.Fatal("Unknown handle type:", val)
	return ""
}

func compileType(val types.Type) Type {
	var r string
	switch val.Kind {
	case types.ArrayType:
		t := compileType(*val.ElementType)
		r = fmt.Sprintf("[%s; %s]", t, compileConstant(*val.ElementCount))
	case types.VectorType:
		t := compileType(*val.ElementType)
		r = fmt.Sprintf("::std::vec::Vec<%s>", t)
	case types.StringType:
		r = "::std::string::String"
	case types.HandleType:
		r = fmt.Sprintf("::zx::%s", compileHandleSubtype(val.HandleSubtype))
	case types.RequestType:
		r = compileCompoundIdentifier(val.RequestSubtype)
	case types.PrimitiveType:
		r = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := compileCompoundIdentifier(val.Identifier)
		// TODO(cramertj): Need to distinguish between interfaces and structs
		r = fmt.Sprintf("::std::boxed::Box<%s>", t)
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}

	if val.Nullable {
		r = fmt.Sprintf("::std::option::Option<%s>", r)
	}
	return Type(r)
}

func compileEnum(val types.Enum) Enum {
	e := Enum{
		compileCamelIdentifier(val.Name),
		compilePrimitiveSubtype(val.Type),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			compileCamelIdentifier(v.Name),
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
			changeIfReserved(v.Name),
		}
		r = append(r, p)
	}

	return r
}

func compileInterface(val types.Interface) Interface {
	r := Interface{
		compileCamelIdentifier(val.Name),
		compileCamelIdentifier(val.Name + "Proxy"),
		compileCamelIdentifier(val.Name + "Stub"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := compileSnakeIdentifier(v.Name)
		m := Method{
			Ordinal:     v.Ordinal,
			Name:        name,
			HasRequest:  v.HasRequest,
			Request:     compileParameterArray(v.Request),
			HasResponse: v.HasResponse,
			Response:    compileParameterArray(v.Response),
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func compileStructMember(val types.StructMember) StructMember {
	return StructMember{
		Type: compileType(val.Type),
		Name: compileSnakeIdentifier(val.Name),
	}
}

func compileStruct(val types.Struct) Struct {
	name := compileCamelIdentifier(val.Name)
	r := Struct{
		Name:    name,
		Members: []StructMember{},
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, compileStructMember(v))
	}

	return r
}

func compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		compileType(val.Type),
		compileSnakeIdentifier(val.Name),
	}
}

func compileUnion(val types.Union) Union {
	r := Union{
		compileCamelIdentifier(val.Name),
		[]UnionMember{},
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, compileUnionMember(v))
	}

	return r
}

func Compile(fidlData types.Root) Root {
	root := Root{}

	// TODO(cramertj): Constants.

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
