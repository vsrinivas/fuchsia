// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"sort"
	"strings"
)

type Type struct {
	Decl       string
	DeclType   types.DeclType
	LargeArray bool
}

type Bits struct {
	types.Attributes
	Name    string
	Type    Type
	Members []BitsMember
}

type BitsMember struct {
	types.Attributes
	Name      string
	ConstName string
	Value     string
}

type Const struct {
	types.Attributes
	Name  string
	Type  string
	Value string
}

type Enum struct {
	types.Attributes
	Name    string
	Type    string
	Members []EnumMember
}

type EnumMember struct {
	types.Attributes
	Name      string
	ConstName string
	Value     string
}

type XUnion struct {
	types.Attributes
	Name    string
	Members []XUnionMember
}

type XUnionMember struct {
	types.Attributes
	Type    string
	Name    string
	Ordinal int
}

type Result struct {
	types.Attributes
	Name      string
	Ok        []string
	Err       UnionMember
	Size      int
	Alignment int
}

type Union struct {
	types.Attributes
	Name      string
	Members   []UnionMember
	Size      int
	Alignment int
}

type UnionMember struct {
	types.Attributes
	Type   Type
	Name   string
	Offset int
}

type Struct struct {
	types.Attributes
	Name        string
	Members     []StructMember
	Size        int
	Alignment   int
	LargeArrays bool
}

type StructMember struct {
	types.Attributes
	Type         string
	Name         string
	Offset       int
	HasDefault   bool
	DefaultValue string
	LargeArray   bool
}

type Table struct {
	types.Attributes
	Name    string
	Members []TableMember
}

type TableMember struct {
	types.Attributes
	Type    string
	Name    string
	Ordinal int
}

type Interface struct {
	types.Attributes
	Name        string
	Methods     []Method
	ServiceName string
}

type Method struct {
	types.Attributes
	Ordinal     types.Ordinal
	GenOrdinal  types.Ordinal
	OrdinalName string
	Name        string
	CamelName   string
	HasRequest  bool
	Request     []Parameter
	HasResponse bool
	Response    []Parameter
}

type Parameter struct {
	Type         string
	BorrowedType string
	Name         string
	Offset       int
}

type Root struct {
	ExternCrates []string
	Bits         []Bits
	Consts       []Const
	Enums        []Enum
	Structs      []Struct
	XUnions      []XUnion
	Unions       []Union
	Results      []Result
	Tables       []Table
	Interfaces   []Interface
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
	"Ok":      true,
	"Err":     true,
	"Vec":     true,
	"Option":  true,
	"Some":    true,
	"None":    true,
	"Box":     true,
	"Future":  true,
	"Stream":  true,
	"Never":   true,
	"Send":    true,
	"fidl":    true,
	"futures": true,
	"zx":      true,
	"async":   true,
	"on_open": true,
	"OnOpen":  true,
}

var reservedSuffixes = []string{
	"Impl",
	"Marker",
	"Proxy",
	"ProxyInterface",
	"ControlHandle",
	"Responder",
	"Server",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func hasReservedSuffix(str string) bool {
	for _, suffix := range reservedSuffixes {
		if strings.HasSuffix(str, suffix) {
			return true
		}
	}
	return false
}

func changeIfReserved(val types.Identifier) string {
	str := string(val)
	if hasReservedSuffix(str) || isReservedWord(str) {
		return str + "_"
	}
	return str
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
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
	decls        *types.DeclMap
	library      types.LibraryIdentifier
	externCrates map[string]bool
}

func (c *compiler) inExternalLibrary(ci types.CompoundIdentifier) bool {
	if len(ci.Library) != len(c.library) {
		return true
	}
	for i, part := range c.library {
		if ci.Library[i] != part {
			return true
		}
	}
	return false
}

func compileCamelIdentifier(val types.Identifier) string {
	return common.ToUpperCamelCase(changeIfReserved(val))
}

func compileLibraryName(library types.LibraryIdentifier) string {
	parts := []string{"fidl"}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(types.Identifier(strings.Join(parts, "_")))
}

func compileSnakeIdentifier(val types.Identifier) string {
	return common.ToSnakeCase(changeIfReserved(val))
}

func compileScreamingSnakeIdentifier(val types.Identifier) string {
	return common.ConstNameToAllCapsSnake(changeIfReserved(val))
}

func (c *compiler) compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	if c.inExternalLibrary(val) {
		externName := compileLibraryName(val.Library)
		c.externCrates[externName] = true
		strs = append(strs, externName)
	}
	str := changeIfReserved(val.Name)
	strs = append(strs, str)
	return strings.Join(strs, "::")
}

func (c *compiler) compileCamelCompoundIdentifier(eci types.EncodedCompoundIdentifier) string {
	val := types.ParseCompoundIdentifier(eci)
	val.Name = types.Identifier(compileCamelIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileSnakeCompoundIdentifier(eci types.EncodedCompoundIdentifier) string {
	val := types.ParseCompoundIdentifier(eci)
	val.Name = types.Identifier(compileSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileScreamingSnakeCompoundIdentifier(eci types.EncodedCompoundIdentifier) string {
	val := types.ParseCompoundIdentifier(eci)
	val.Name = types.Identifier(compileScreamingSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func compileLiteral(val types.Literal, typ types.Type) string {
	switch val.Kind {
	case types.StringLiteral:
		return fmt.Sprintf("r###\"%s\"###", val.Value)
	case types.NumericLiteral:
		if typ.Kind == types.PrimitiveType &&
			(typ.PrimitiveSubtype == types.Float32 || typ.PrimitiveSubtype == types.Float64) {
			if !strings.ContainsRune(val.Value, '.') {
				return fmt.Sprintf("%s.0", val.Value)
			}
			return val.Value
		}
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.DefaultLiteral:
		return "::Default::default()"
	default:
		log.Fatal("Unknown literal kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant, typ types.Type) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileScreamingSnakeCompoundIdentifier(val.Identifier)
	case types.LiteralConstant:
		return compileLiteral(val.Literal, typ)
	default:
		log.Fatal("Unknown constant kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConst(val types.Const) Const {
	name := c.compileScreamingSnakeCompoundIdentifier(val.Name)
	var r Const
	if val.Type.Kind == types.StringType {
		r = Const{
			Attributes: val.Attributes,
			Type:       "&str",
			Name:       name,
			Value:      c.compileConstant(val.Value, val.Type),
		}
	} else {
		r = Const{
			Attributes: val.Attributes,
			Type:       c.compileType(val.Type, false).Decl,
			Name:       name,
			Value:      c.compileConstant(val.Value, val.Type),
		}
	}
	return r
}

func compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type: ", val)
	return ""
}

func compileHandleSubtype(val types.HandleSubtype) string {
	if t, ok := handleSubtypes[val]; ok {
		return t
	}
	log.Fatal("Unknown handle type: ", val)
	return ""
}

func (c *compiler) compileType(val types.Type, borrowed bool) Type {
	var r string
	var declType types.DeclType
	largeArray := false
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType, borrowed)
		r = fmt.Sprintf("[%s; %v]", t.Decl, *val.ElementCount)
		if borrowed {
			r = fmt.Sprintf("&mut %s", r)
		}
		largeArray = t.LargeArray || *val.ElementCount > 32
	case types.VectorType:
		t := c.compileType(*val.ElementType, borrowed)
		var inner string
		if borrowed {
			inner = fmt.Sprintf("&mut ExactSizeIterator<Item = %s>", t.Decl)
		} else {
			inner = fmt.Sprintf("Vec<%s>", t.Decl)
		}
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", inner)
		} else {
			r = inner
		}
		largeArray = t.LargeArray
	case types.StringType:
		if borrowed {
			if val.Nullable {
				r = "Option<&str>"
			} else {
				r = "&str"
			}
		} else {
			if val.Nullable {
				r = "Option<String>"
			} else {
				r = "String"
			}
		}
	case types.HandleType:
		r = fmt.Sprintf("zx::%s", compileHandleSubtype(val.HandleSubtype))
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", r)
		}
	case types.RequestType:
		r = c.compileCamelCompoundIdentifier(val.RequestSubtype)
		r = fmt.Sprintf("fidl::endpoints::ServerEnd<%sMarker>", r)
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", r)
		}
	case types.PrimitiveType:
		// Primitive types are small, simple, and never contain handles,
		// so there's no need to borrow them
		r = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := c.compileCamelCompoundIdentifier(val.Identifier)
		declType, ok := (*c.decls)[val.Identifier]
		if !ok {
			log.Fatal("unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.BitsDeclType:
			fallthrough
		case types.EnumDeclType:
			// Bits and enums are small, simple, and never contain handles,
			// so no need to borrow
			borrowed = false
			fallthrough
		case types.ConstDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.UnionDeclType:
			if val.Nullable {
				if borrowed {
					r = fmt.Sprintf("Option<fidl::encoding::OutOfLine<%s>>", t)
				} else {
					r = fmt.Sprintf("Option<Box<%s>>", t)
				}
			} else {
				if borrowed {
					r = fmt.Sprintf("&mut %s", t)
				} else {
					r = t
				}
			}
		case types.XUnionDeclType:
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", t)
			} else {
				if borrowed {
					r = fmt.Sprintf("&mut %s", t)
				} else {
					r = t
				}
			}
		case types.TableDeclType:
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", t)
			} else {
				r = t
			}
		case types.InterfaceDeclType:
			r = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", r)
			}
		default:
			log.Fatal("Unknown declaration type in interface: ", declType)
		}
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}

	return Type{
		Decl:       r,
		DeclType:   declType,
		LargeArray: largeArray,
	}
}

func (c *compiler) compileBits(val types.Bits) Bits {
	e := Bits{
		val.Attributes,
		c.compileCamelCompoundIdentifier(val.Name),
		c.compileType(val.Type, false),
		[]BitsMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, BitsMember{
			Attributes: v.Attributes,
			Name:       compileCamelIdentifier(v.Name),
			ConstName:  compileScreamingSnakeIdentifier(v.Name),
			Value:      c.compileConstant(v.Value, val.Type),
		})
	}
	return e
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	e := Enum{
		val.Attributes,
		c.compileCamelCompoundIdentifier(val.Name),
		compilePrimitiveSubtype(val.Type),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			Attributes: v.Attributes,
			Name:       compileCamelIdentifier(v.Name),
			ConstName:  compileScreamingSnakeIdentifier(v.Name),
			// TODO(FIDL-324): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, types.Type{
				Kind:             types.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
		})
	}
	return e
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			c.compileType(v.Type, false).Decl,
			c.compileType(v.Type, true).Decl,
			compileSnakeIdentifier(v.Name),
			v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		val.Attributes,
		c.compileCamelCompoundIdentifier(val.Name),
		[]Method{},
		strings.Trim(val.GetServiceName(), "\""),
	}

	for _, v := range val.Methods {
		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		request := c.compileParameterArray(v.Request)
		response := c.compileParameterArray(v.Response)

		m := Method{
			Attributes:  v.Attributes,
			Ordinal:     v.Ordinal,
			GenOrdinal:  v.GenOrdinal,
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
	memberType := c.compileType(val.Type, false)
	return StructMember{
		Attributes:   val.Attributes,
		Type:         memberType.Decl,
		Name:         compileSnakeIdentifier(val.Name),
		Offset:       val.Offset,
		HasDefault:   false,
		DefaultValue: "", // TODO(cramertj) support defaults
		LargeArray:   memberType.LargeArray,
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	name := c.compileCamelCompoundIdentifier(val.Name)
	r := Struct{
		Attributes:  val.Attributes,
		Name:        name,
		Members:     []StructMember{},
		Size:        val.Size,
		Alignment:   val.Alignment,
		LargeArrays: false,
	}

	for _, v := range val.Members {
		member := c.compileStructMember(v)
		r.Members = append(r.Members, member)
		r.LargeArrays = r.LargeArrays || member.LargeArray
	}

	return r
}

func (c *compiler) compileXUnionMember(val types.XUnionMember) XUnionMember {
	return XUnionMember{
		Attributes: val.Attributes,
		Type:       c.compileType(val.Type, false).Decl,
		Name:       compileCamelIdentifier(val.Name),
		Ordinal:    val.Ordinal,
	}
}

func (c *compiler) compileXUnion(val types.XUnion) XUnion {
	r := XUnion{
		Attributes: val.Attributes,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Members:    []XUnionMember{},
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileXUnionMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		Attributes: val.Attributes,
		Type:       c.compileType(val.Type, false),
		Name:       compileCamelIdentifier(val.Name),
		Offset:     val.Offset,
	}
}

func (c *compiler) compileResult(val types.Union, root Root) Result {
	r := Result{
		Attributes: val.Attributes,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Ok:         []string{},
		Err:        c.compileUnionMember(val.Members[1]),
		Size:       val.Size,
		Alignment:  val.Alignment,
	}

	OkArm := val.Members[0]
	ci := c.compileCamelCompoundIdentifier(OkArm.Type.Identifier)

	// always a struct on the Ok arms in Results
	for _, v := range root.Structs {
		if v.Name == ci {
			for _, m := range v.Members {
				r.Ok = append(r.Ok, m.Type)
			}
		}
	}

	return r
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		Attributes: val.Attributes,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Members:    []UnionMember{},
		Size:       val.Size,
		Alignment:  val.Alignment,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func (c *compiler) compileTable(table types.Table) Table {
	var members []TableMember
	for _, member := range table.Members {
		if member.Reserved {
			continue
		}
		members = append(members, TableMember{
			Attributes: member.Attributes,
			Type:       c.compileType(member.Type, false).Decl,
			Name:       compileSnakeIdentifier(member.Name),
			Ordinal:    member.Ordinal,
		})
	}
	return Table{
		Attributes: table.Attributes,
		Name:       c.compileCamelCompoundIdentifier(table.Name),
		Members:    members,
	}
}

func Compile(r types.Root) Root {
	root := Root{}
	thisLibParsed := types.ParseLibraryName(r.Name)
	c := compiler{&r.Decls, thisLibParsed, map[string]bool{}}

	for _, v := range r.Bits {
		root.Bits = append(root.Bits, c.compileBits(v))
	}

	for _, v := range r.Consts {
		root.Consts = append(root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range r.Interfaces {
		root.Interfaces = append(root.Interfaces, c.compileInterface(v))
	}

	for _, v := range r.Structs {
		root.Structs = append(root.Structs, c.compileStruct(v))
	}

	for _, v := range r.XUnions {
		root.XUnions = append(root.XUnions, c.compileXUnion(v))
	}

	for _, v := range r.Unions {
		// Results are a specialized type of Union
		if v.Attributes.HasAttribute("Result") {
			root.Results = append(root.Results, c.compileResult(v, root))
		} else {
			root.Unions = append(root.Unions, c.compileUnion(v))
		}
	}

	for _, v := range r.Tables {
		root.Tables = append(root.Tables, c.compileTable(v))
	}

	thisLibCompiled := compileLibraryName(thisLibParsed)

	// Sort the extern crates to make sure the generated file is
	// consistent across builds.
	var externCrates []string
	for k, _ := range c.externCrates {
		externCrates = append(externCrates, k)
	}
	sort.Strings(externCrates)

	for _, k := range externCrates {
		if k != thisLibCompiled {
			root.ExternCrates = append(root.ExternCrates, k)
		}
	}

	return root
}
