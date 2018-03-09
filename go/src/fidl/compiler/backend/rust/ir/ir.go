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

type Type struct {
	Decl     string
	DeclType types.DeclType
}

type Const struct {
	Name  string
	Type  string
	Value string
}

type Enum struct {
	Name    string
	Type    string
	Members []EnumMember
}

type EnumMember struct {
	Name      string
	ConstName string
	Value     string
}

type Union struct {
	Name      string
	Members   []UnionMember
	Size      int
	Alignment int
}

type UnionMember struct {
	Type   string
	Name   string
	Offset int
}

type Struct struct {
	Name      string
	Members   []StructMember
	Size      int
	Alignment int
}

type StructMember struct {
	Type         string
	Name         string
	Offset       int
	HasDefault   bool
	DefaultValue string
}

type Interface struct {
	Name    string
	Methods []Method
}

type Method struct {
	Ordinal      types.Ordinal
	OrdinalName  string
	Name         string
	CamelName    string
	HasRequest   bool
	Request      []Parameter
	HasResponse  bool
	Response     []Parameter
	ResponseSize int
}

type Parameter struct {
	Type   string
	Name   string
	Offset int
}

type Root struct {
	Consts     []Const
	Enums      []Enum
	Structs    []Struct
	Unions     []Union
	Interfaces []Interface
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
	"Ok":         true,
	"Err":        true,
	"Vec":        true,
	"Option":     true,
	"Some":       true,
	"None":       true,
	"Box":        true,
	"Future":     true,
	"Stream":     true,
	"Never":      true,
	"fidl":       true,
	"futures":    true,
	"zx":         true,
	"response":   true,
	"controller": true,
	"async":      true,

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
	types.Handle:    "Handle",
	types.Process:   "Process",
	types.Thread:    "Thread",
	types.Vmo:       "Vmo",
	types.Channel:   "Channel",
	types.Event:     "Event",
	types.Port:      "Port",
	types.Interrupt: "Interrupt",
	types.Log:       "Log",
	types.Socket:    "Socket",
	types.Resource:  "Resource",
	types.Eventpair: "EventPair",
	types.Job:       "Job",
	types.Vmar:      "Vmar",
	types.Fifo:      "Fifo",
	types.Guest:     "Guest",
	types.Time:      "Timer",
}

type compiler struct {
	decls *types.DeclMap
}

func compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for i, v := range val {
		str := changeIfReserved(v)
		if i == (len(val) - 1) {
			// The last component should be camel-cased
			str = common.ToUpperCamelCase(str)
		}
		strs = append(strs, str)
	}
	return strings.Join(strs, "::")
}

func compileCamelIdentifier(val types.Identifier) string {
	return common.ToUpperCamelCase(changeIfReserved(val))
}

func compileSnakeIdentifier(val types.Identifier) string {
	return common.ToSnakeCase(changeIfReserved(val))
}

func compileScreamingSnakeIdentifier(val types.Identifier) string {
	return common.ConstNameToAllCapsSnake(changeIfReserved(val))
}

func compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		return fmt.Sprintf("r###\"%s\"###", val.Value)
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
		return compileScreamingSnakeIdentifier(
			types.Identifier(compileCompoundIdentifier(val.Identifier)))
	case types.LiteralConstant:
		return compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compileConst(val types.Const) Const {
	var r Const
	if val.Type.Kind == types.StringType {
		r = Const{
			Type:  "&str",
			Name:  compileScreamingSnakeIdentifier(val.Name[0]),
			Value: compileConstant(val.Value),
		}
	} else {
		r = Const{
			Type:  c.compileType(val.Type).Decl,
			Name:  compileScreamingSnakeIdentifier(val.Name[0]),
			Value: compileConstant(val.Value),
		}
	}
	return r
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

func (c *compiler) compileType(val types.Type) Type {
	var r string
	var declType types.DeclType
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType)
		r = fmt.Sprintf("[%s; %v]", t.Decl, *val.ElementCount)
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		r = fmt.Sprintf("Vec<%s>", t.Decl)
	case types.StringType:
		r = "String"
	case types.HandleType:
		r = fmt.Sprintf("::zx::%s", compileHandleSubtype(val.HandleSubtype))
	case types.RequestType:
		r = compileCompoundIdentifier(val.RequestSubtype)
	case types.PrimitiveType:
		r = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := compileCompoundIdentifier(val.Identifier)
		declType, ok := (*c.decls)[val.Identifier[0]]
		if !ok {
			log.Fatal("unknown identifier:", val.Identifier)
		}
		switch declType {
		case types.ConstDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.UnionDeclType:
			if val.Nullable {
				r = fmt.Sprintf("Option<Box<%s>>", t)
			} else {
				r = t
			}
		case types.InterfaceDeclType:
			r = fmt.Sprintf("::fidl::encoding2::InterfaceHandle<%s>", t)
		default:
			log.Fatal("Unknown declaration type: ", declType)
		}
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}

	return Type{
		Decl:     r,
		DeclType: declType,
	}
}

func compileEnum(val types.Enum) Enum {
	e := Enum{
		compileCamelIdentifier(val.Name[0]),
		compilePrimitiveSubtype(val.Type),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			Name:      compileCamelIdentifier(v.Name),
			ConstName: compileScreamingSnakeIdentifier(v.Name),
			Value:     compileConstant(v.Value),
		})
	}
	return e
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			c.compileType(v.Type).Decl,
			changeIfReserved(v.Name),
			v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		compileCamelIdentifier(val.Name[0]),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		request := c.compileParameterArray(v.Request)
		response := c.compileParameterArray(v.Response)

		m := Method{
			Ordinal:     v.Ordinal,
			Name:        name,
			CamelName:   camelName,
			HasRequest:  v.HasRequest,
			Request:     request,
			HasResponse: v.HasResponse,
			Response:    response,
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	return StructMember{
		Type:         c.compileType(val.Type).Decl,
		Name:         compileSnakeIdentifier(val.Name),
		Offset:       val.Offset,
		HasDefault:   false,
		DefaultValue: "", // TODO(cramertj) support defaults
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	name := compileCamelIdentifier(val.Name[0])
	r := Struct{
		Name:      name,
		Members:   []StructMember{},
		Size:      val.Size,
		Alignment: val.Alignment,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		Type:   c.compileType(val.Type).Decl,
		Name:   compileCamelIdentifier(val.Name),
		Offset: val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		Name:      compileCamelIdentifier(val.Name[0]),
		Members:   []UnionMember{},
		Size:      val.Size,
		Alignment: val.Alignment,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func Compile(r types.Root) Root {
	root := Root{}
	c := compiler{&r.Decls}

	for _, v := range r.Consts {
		root.Consts = append(root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		root.Enums = append(root.Enums, compileEnum(v))
	}

	for _, v := range r.Interfaces {
		root.Interfaces = append(root.Interfaces, c.compileInterface(v))
	}

	for _, v := range r.Structs {
		root.Structs = append(root.Structs, c.compileStruct(v))
	}

	for _, v := range r.Unions {
		root.Unions = append(root.Unions, c.compileUnion(v))
	}

	return root
}
