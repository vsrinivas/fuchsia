// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"sort"
	"strings"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
)

type EncodedCompoundIdentifier = types.EncodedCompoundIdentifier

type Type struct {
	Decl     string
	DeclType types.DeclType
	Derives  derives
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
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []XUnionMember
	types.Strictness
}

type XUnionMember struct {
	types.Attributes
	Type    string
	OGType  types.Type
	Name    string
	Ordinal int
}

type Result struct {
	types.Attributes
	ECI       EncodedCompoundIdentifier
	Derives   derives
	Name      string
	OkOGTypes []types.Type
	Ok        []string
	ErrOGType types.Type
	ErrType   string
	Size      int
	Alignment int
}

type Union struct {
	types.Attributes
	ECI       EncodedCompoundIdentifier
	Derives   derives
	Name      string
	Members   []UnionMember
	Size      int
	Alignment int
}

type UnionMember struct {
	types.Attributes
	Derives       derives
	OGType        types.Type
	Type          Type
	Name          string
	Offset        int
	XUnionOrdinal int
}

type Struct struct {
	types.Attributes
	ECI     EncodedCompoundIdentifier
	Derives derives
	Name    string
	Members []StructMember
	// Store size and alignment for Old and V1 versions of the wire format. The
	// numbers will be different if the struct contains a union within it. Only
	// structs have this information because fidl::encoding only uses these
	// precalculated numbers for structs and unions.
	SizeOld      int
	AlignmentOld int
	SizeV1       int
	AlignmentV1  int
}

type StructMember struct {
	types.Attributes
	OGType       types.Type
	Type         string
	Name         string
	OffsetOld    int
	OffsetV1     int
	HasDefault   bool
	DefaultValue string
}

type Table struct {
	types.Attributes
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []TableMember
}

type TableMember struct {
	types.Attributes
	OGType  types.Type
	Type    string
	Name    string
	Ordinal int
}

type Interface struct {
	types.Attributes
	RequestDerives derives
	EventDerives   derives
	ECI            EncodedCompoundIdentifier
	Name           string
	Methods        []Method
	ServiceName    string
}

type Method struct {
	types.Attributes
	Ordinals    types.Ordinals
	Name        string
	CamelName   string
	HasRequest  bool
	Request     []Parameter
	HasResponse bool
	Response    []Parameter
}

type Parameter struct {
	OGType       types.Type
	Type         string
	BorrowedType string
	Name         string
	Offset       int
}

type Service struct {
	types.Attributes
	Name        string
	Members     []ServiceMember
	ServiceName string
}

type ServiceMember struct {
	types.Attributes
	InterfaceType string
	Name          string
	CamelName     string
	SnakeName     string
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
	Services     []Service
}

func (r *Root) findInterface(eci EncodedCompoundIdentifier) *Interface {
	for i := range r.Interfaces {
		if r.Interfaces[i].ECI == eci {
			return &r.Interfaces[i]
		}
	}
	return nil
}

func (r *Root) findStruct(eci EncodedCompoundIdentifier) *Struct {
	for i := range r.Structs {
		if r.Structs[i].ECI == eci {
			return &r.Structs[i]
		}
	}
	return nil
}

func (r *Root) findTable(eci EncodedCompoundIdentifier) *Table {
	for i := range r.Tables {
		if r.Tables[i].ECI == eci {
			return &r.Tables[i]
		}
	}
	return nil
}

func (r *Root) findUnion(eci EncodedCompoundIdentifier) *Union {
	for i := range r.Unions {
		if r.Unions[i].ECI == eci {
			return &r.Unions[i]
		}
	}
	return nil
}

func (r *Root) findResult(eci EncodedCompoundIdentifier) *Result {
	for i := range r.Results {
		if r.Results[i].ECI == eci {
			return &r.Results[i]
		}
	}
	return nil
}

func (r *Root) findXUnion(eci EncodedCompoundIdentifier) *XUnion {
	for i := range r.XUnions {
		if r.XUnions[i].ECI == eci {
			return &r.XUnions[i]
		}
	}
	return nil
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
	"Result":  true,
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
	types.Bti:          "Bti",
	types.Channel:      "Channel",
	types.DebugLog:     "DebugLog",
	types.Event:        "Event",
	types.Eventpair:    "EventPair",
	types.Exception:    "Exception",
	types.Fifo:         "Fifo",
	types.Guest:        "Guest",
	types.Handle:       "Handle",
	types.Interrupt:    "Interrupt",
	types.Iommu:        "Iommu",
	types.Job:          "Job",
	types.Pager:        "Pager",
	types.PciDevice:    "PciDevice",
	types.Pmt:          "Pmt",
	types.Port:         "Port",
	types.Process:      "Process",
	types.Profile:      "Profile",
	types.Resource:     "Resource",
	types.Socket:       "Socket",
	types.SuspendToken: "SuspendToken",
	types.Thread:       "Thread",
	types.Time:         "Timer",
	types.Vcpu:         "Vcpu",
	types.Vmar:         "Vmar",
	types.Vmo:          "Vmo",
}

type compiler struct {
	decls        types.DeclMap
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
	if val.Member != "" {
		strs = append(strs, string(val.Member))
	}
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
		log.Panic("Unknown literal kind: ", val.Kind)
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
		log.Panic("Unknown constant kind: ", val.Kind)
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
	log.Panic("Unknown primitive type: ", val)
	return ""
}

func compileHandleSubtype(val types.HandleSubtype) string {
	if t, ok := handleSubtypes[val]; ok {
		return t
	}
	log.Panic("Unknown handle type: ", val)
	return ""
}

func (c *compiler) compileType(val types.Type, borrowed bool) Type {
	var r string
	var declType types.DeclType
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType, borrowed)
		r = fmt.Sprintf("[%s; %v]", t.Decl, *val.ElementCount)
		if borrowed {
			r = fmt.Sprintf("&mut %s", r)
		}
	case types.VectorType:
		t := c.compileType(*val.ElementType, borrowed)
		var inner string
		if borrowed {
			inner = fmt.Sprintf("&mut dyn ExactSizeIterator<Item = %s>", t.Decl)
		} else {
			inner = fmt.Sprintf("Vec<%s>", t.Decl)
		}
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", inner)
		} else {
			r = inner
		}
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
		r = fmt.Sprintf("fidl::%s", compileHandleSubtype(val.HandleSubtype))
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
		declType, ok := c.decls[val.Identifier]
		if !ok {
			log.Panic("unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.BitsDeclType, types.EnumDeclType:
			// Bits and enums are small, simple, and never contain handles,
			// so no need to borrow
			borrowed = false
			fallthrough
		case types.ConstDeclType, types.StructDeclType, types.XUnionDeclType:
			if val.Nullable {
				if borrowed {
					// TODO(fxb/42304): Replace with "Option<&mut %s>".
					r = fmt.Sprintf("Option<fidl::encoding::OutOfLine<'_, %s>>", t)
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
		// TODO(fxb/42304): Combine this into the case above.
		case types.UnionDeclType:
			if val.Nullable {
				if borrowed {
					wrapper := c.compileCompoundIdentifier(types.CompoundIdentifier{
						Library: types.ParseCompoundIdentifier(val.Identifier).Library,
						Name:    "OutOfLineUnion",
					})
					r = fmt.Sprintf("Option<%s<'_, %s>>", wrapper, t)
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
		case types.TableDeclType:
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", t)
			} else {
				// TODO(fxb/42304): Replace with "&mut %s".
				r = t
			}
		case types.InterfaceDeclType:
			r = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", r)
			}
		default:
			log.Panic("Unknown declaration type in interface: ", declType)
		}
	default:
		log.Panic("Unknown type kind: ", val.Kind)
	}

	return Type{
		Decl:     r,
		DeclType: declType,
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
			OGType:       v.Type,
			Type:         c.compileType(v.Type, false).Decl,
			BorrowedType: c.compileType(v.Type, true).Decl,
			Name:         compileSnakeIdentifier(v.Name),
			Offset:       v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		Attributes:  val.Attributes,
		ECI:         val.Name,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Methods:     []Method{},
		ServiceName: strings.Trim(val.GetServiceName(), "\""),
	}

	for _, v := range val.Methods {
		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		request := c.compileParameterArray(v.Request)
		response := c.compileParameterArray(v.Response)

		m := Method{
			Attributes:  v.Attributes,
			Ordinals:    types.NewOrdinalsStep7(v, "UNUSED", "UNUSED"),
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

func (c *compiler) compileService(val types.Service) Service {
	r := Service{
		Attributes:  val.Attributes,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Members:     []ServiceMember{},
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		m := ServiceMember{
			Attributes:    v.Attributes,
			Name:          string(v.Name),
			CamelName:     compileCamelIdentifier(v.Name),
			SnakeName:     compileSnakeIdentifier(v.Name),
			InterfaceType: c.compileCamelCompoundIdentifier(v.Type.Identifier),
		}
		r.Members = append(r.Members, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	memberType := c.compileType(val.Type, false)
	return StructMember{
		Attributes:   val.Attributes,
		Type:         memberType.Decl,
		OGType:       val.Type,
		Name:         compileSnakeIdentifier(val.Name),
		OffsetOld:    val.FieldShapeOld.Offset,
		OffsetV1:     val.FieldShapeV1.Offset,
		HasDefault:   false,
		DefaultValue: "", // TODO(cramertj) support defaults
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	name := c.compileCamelCompoundIdentifier(val.Name)
	r := Struct{
		Attributes:   val.Attributes,
		ECI:          val.Name,
		Name:         name,
		Members:      []StructMember{},
		SizeOld:      val.TypeShapeOld.InlineSize,
		AlignmentOld: val.TypeShapeOld.Alignment,
		SizeV1:       val.TypeShapeV1.InlineSize,
		AlignmentV1:  val.TypeShapeV1.Alignment,
	}

	for _, v := range val.Members {
		member := c.compileStructMember(v)
		r.Members = append(r.Members, member)
	}

	return r
}

func (c *compiler) compileXUnionMember(val types.XUnionMember) XUnionMember {
	return XUnionMember{
		Attributes: val.Attributes,
		Type:       c.compileType(val.Type, false).Decl,
		OGType:     val.Type,
		Name:       compileCamelIdentifier(val.Name),
		Ordinal:    val.Ordinal,
	}
}

func (c *compiler) compileXUnion(val types.XUnion) XUnion {
	r := XUnion{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Members:    []XUnionMember{},
		Strictness: val.Strictness,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileXUnionMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		Attributes:    val.Attributes,
		OGType:        val.Type,
		Type:          c.compileType(val.Type, false),
		Name:          compileCamelIdentifier(val.Name),
		Offset:        val.Offset,
		XUnionOrdinal: val.XUnionOrdinal,
	}
}

func (c *compiler) compileResultFromUnion(val types.Union, root Root) Result {
	r := Result{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		OkOGTypes:  []types.Type{},
		Ok:         []string{},
		ErrOGType:  val.Members[1].Type,
		ErrType:    c.compileUnionMember(val.Members[1]).Type.Decl,
		Size:       val.Size,
		Alignment:  val.Alignment,
	}

	OkArm := val.Members[0]
	ci := c.compileCamelCompoundIdentifier(OkArm.Type.Identifier)

	// always a struct on the Ok arms in Results
	for _, v := range root.Structs {
		if v.Name == ci {
			for _, m := range v.Members {
				r.OkOGTypes = append(r.OkOGTypes, m.OGType)
				r.Ok = append(r.Ok, m.Type)
			}
		}
	}

	return r
}

func (c *compiler) compileResultFromXUnion(val types.XUnion, root Root) Result {
	r := Result{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		OkOGTypes:  []types.Type{},
		Ok:         []string{},
		ErrOGType:  val.Members[1].Type,
		ErrType:    c.compileXUnionMember(val.Members[1]).Type,
		Size:       val.Size,
		Alignment:  val.Alignment,
	}

	OkArm := val.Members[0]
	ci := c.compileCamelCompoundIdentifier(OkArm.Type.Identifier)

	// always a struct on the Ok arms in Results
	for _, v := range root.Structs {
		if v.Name == ci {
			for _, m := range v.Members {
				r.OkOGTypes = append(r.OkOGTypes, m.OGType)
				r.Ok = append(r.Ok, m.Type)
			}
		}
	}

	return r
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Members:    []UnionMember{},
		Size:       val.Size,
		Alignment:  val.Alignment,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func (c *compiler) compileTable(table types.Table) Table {
	var members []TableMember
	for _, member := range table.SortedMembersNoReserved() {
		members = append(members, TableMember{
			Attributes: member.Attributes,
			OGType:     member.Type,
			Type:       c.compileType(member.Type, false).Decl,
			Name:       compileSnakeIdentifier(member.Name),
			Ordinal:    member.Ordinal,
		})
	}
	return Table{
		Attributes: table.Attributes,
		ECI:        table.Name,
		Name:       c.compileCamelCompoundIdentifier(table.Name),
		Members:    members,
	}
}

type derives uint16

// FIXME(cramertj) remove `Debug`, `Hash`, and `PartialEq` when impl'd for large arrays
const (
	derivesDebug derives = 1 << iota
	derivesCopy
	derivesClone
	derivesEq
	derivesPartialEq
	derivesOrd
	derivesPartialOrd
	derivesHash
	derivesAll derives = (1 << iota) - 1
	// note: ensure any new flags don't outnumber the number of bits in `derives`
)

func newDerives(values ...derives) derives {
	var v derives
	for i := 0; i < len(values); i++ {
		v |= values[i]
	}
	return v
}

func (v derives) and(others derives) derives {
	return v & others
}

func (v derives) remove(others derives) derives {
	return v & ^others
}

func (v derives) andUnknown() derives {
	// FIXME(cramertj): properly, this should set everything to false
	// since e.g. a new table member could be added containing a large
	// array, which would be a breaking change due to the removal of the
	// `Debug` impl. However, soon enough every type will implement
	// `Debug`, `PartialEq`, and `Hash` due to the automatic impls for arrays.
	// In any case, not having `Debug` for all types containing non-strict
	// tables or xunions would be *extremely* annoying and a massively breaking
	// change, so we leave them as `true` for now.
	return v.and(newDerives(derivesDebug, derivesPartialEq))
}

func (v derives) String() string {
	deriveToName := map[derives]string{
		derivesDebug:      "Debug",
		derivesCopy:       "Copy",
		derivesClone:      "Clone",
		derivesEq:         "Eq",
		derivesPartialEq:  "PartialEq",
		derivesOrd:        "Ord",
		derivesPartialOrd: "PartialOrd",
		derivesHash:       "Hash",
	}
	var parts []string
	for bit := derives(1); bit&derivesAll != 0; bit <<= 1 {
		if v.and(bit) != derives(0) {
			parts = append(parts, deriveToName[bit])
		}
	}
	if len(parts) == 0 {
		return ""
	}
	return fmt.Sprintf("#[derive(%s)]", strings.Join(parts, ", "))
}

// The status of derive calculation for a particular type.
type deriveStatus struct {
	// recursing indicates whether or not we've passed through this type already
	// on a recursive descent. This is used to prevent unbounded recursion on
	// mutually-recursive types.
	recursing bool
	// complete indicates whether or not the derive for the given type has
	// already been successfully calculated and stored in the IR.
	complete bool
}

// The state of the current calculation of derives.
type derivesCompiler struct {
	*compiler
	topMostCall                bool
	didShortCircuitOnRecursion bool
	statuses                   map[EncodedCompoundIdentifier]deriveStatus
	root                       *Root
}

// Calculates what traits should be derived for each output type,
// filling in all `*derives` in the IR.
func (c *compiler) fillDerives(ir *Root) {
	dc := &derivesCompiler{
		compiler:                   c,
		topMostCall:                true,
		didShortCircuitOnRecursion: false,
		statuses:                   make(map[EncodedCompoundIdentifier]deriveStatus),
		root:                       ir,
	}

	// Bits and enums always derive all traits
	for _, v := range ir.Interfaces {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Structs {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.XUnions {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Results {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Unions {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Tables {
		dc.fillDerivesForECI(v.ECI)
	}
}

func (dc *derivesCompiler) fillDerivesForECI(eci EncodedCompoundIdentifier) derives {
	if dc.inExternalLibrary(types.ParseCompoundIdentifier(eci)) {
		// Return the set of derives that we assume external types have.
		// If an externally referenced type fails to have all of these derives
		// present, we may fail compilation. However, `Debug` and `PartialEq`
		// are so enormously valuable and only missing on large arrays, so we
		// assume that they are present.
		//
		// FIXME(cramertj): this is a dirty hack that shouldn't exist-- instead,
		// we should check the list of derives that are actually present
		// for the external type.
		return newDerives(derivesDebug, derivesPartialEq)
	}

	topMostCall := dc.topMostCall
	if dc.topMostCall {
		dc.topMostCall = false
	}

	deriveStatus := dc.statuses[eci]

	if deriveStatus.recursing {
		// If we've already seen the current type while recursing,
		// the algorithm has already explored all of the other fields contained
		// within the cycle, so we can return true for all derives, and the
		// correct results will be bubbled up.
		dc.didShortCircuitOnRecursion = true
		return derivesAll
	}

	deriveStatus.recursing = true
	dc.statuses[eci] = deriveStatus

	declType := dc.decls[eci]
	var derivesOut derives
typeSwitch:
	switch declType {
	case types.ConstDeclType:
		fallthrough
	case types.BitsDeclType:
		fallthrough
	case types.EnumDeclType:
		// Enums and bits are always simple, non-float primitives which
		// implement all derivable traits.
		derivesOut = derivesAll
	case types.InterfaceDeclType:
		// Derives output for interfaces is only used when talking about ClientEnds,
		// which are neither Copy nor Clone. Note: this does *not* refer to the
		// derives used in either the `Request` or `Event` enums, which are the
		// values filled in by this function.
		derivesOut = derivesAll.remove(newDerives(derivesCopy, derivesClone))

		// Check if the derives have already been calculated
		if deriveStatus.complete {
			break typeSwitch
		}

		iface := dc.root.findInterface(eci)
		if iface == nil {
			log.Panic("interface not found: ", eci)
		}
		// Requests and events are at *most* ever Debug.
		// FIXME(cramertj): all of the interface logic here can
		// be removed once all types (large arrays) are Debug,
		// since that's all we care about for interfaces
		requestDerives := derivesDebug
		eventDerives := derivesDebug
		for _, method := range iface.Methods {
			if method.HasRequest {
				// Request enum object-- consider all request data
				for _, requestParam := range method.Request {
					d := dc.fillDerivesForType(requestParam.OGType)
					requestDerives = requestDerives.and(d)
				}
			} else {
				// Event enum object-- consider all response data
				for _, responseParam := range method.Response {
					d := dc.fillDerivesForType(responseParam.OGType)
					eventDerives = eventDerives.and(d)
				}
			}
		}
		iface.RequestDerives = requestDerives
		iface.EventDerives = eventDerives
	case types.StructDeclType:
		st := dc.root.findStruct(eci)
		if st == nil {
			log.Panic("struct not found: ", eci)
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = st.Derives
			break typeSwitch
		}
		derivesOut = derivesAll
		for _, member := range st.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		st.Derives = derivesOut
	case types.TableDeclType:
		table := dc.root.findTable(eci)
		if table == nil {
			log.Panic("table not found: ", eci)
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = table.Derives
			break typeSwitch
		}
		derivesOut = derivesAll
		for _, member := range table.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		// FIXME(cramertj) this should only happen on non-`strict` tables.
		// Non-strict tables aren't Copy because of storing extra bits and handles in vecs.
		// When large arrays are no longer an issue and we stop tracking
		// Debug and PartialEq, this should set all values to false for
		// non-strict tables.
		derivesOut = derivesOut.remove(derivesCopy).andUnknown()
		table.Derives = derivesOut
	case types.UnionDeclType:
		union := dc.root.findUnion(eci)
		var result *Result
		var xunion *XUnion
		if union == nil {
			result = dc.root.findResult(eci)
		}
		if union == nil && result == nil {
			xunion = dc.root.findXUnion(eci)
		}
		if union == nil && result == nil && xunion == nil {
			log.Panic("union not found: ", eci)
		}
		if union != nil {
			// It's a union, not a result
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = union.Derives
				break typeSwitch
			}
			derivesOut = derivesAll
			for _, member := range union.Members {
				derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
			}
			union.Derives = derivesOut
		} else if result != nil {
			// It's a Result, not a union
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = result.Derives
				break typeSwitch
			}
			derivesOut = derivesAll
			for _, oktype := range result.OkOGTypes {
				derivesOut = derivesOut.and(dc.fillDerivesForType(oktype))
			}
			derivesOut = derivesOut.and(dc.fillDerivesForType(result.ErrOGType))
			result.Derives = derivesOut
		} else {
			if deriveStatus.complete {
				derivesOut = xunion.Derives
				break typeSwitch
			}
			derivesOut = derivesAll
			for _, member := range xunion.Members {
				derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
			}
			xunion.Derives = derivesOut
		}
	case types.XUnionDeclType:
		xunion := dc.root.findXUnion(eci)
		if xunion == nil {
			log.Panic("xunion not found: ", eci)
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = xunion.Derives
			break typeSwitch
		}
		derivesOut = derivesAll
		for _, member := range xunion.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		if !xunion.Strictness {
			// FIXME(cramertj) When large arrays are no longer an issue and we
			// stop tracking Debug and PartialEq, this should set all values to
			// false for non-strict xunions.
			if !xunion.Strictness {
				derivesOut = derivesOut.remove(derivesCopy).andUnknown()
			}
		}
		xunion.Derives = derivesOut
	default:
		log.Panic("Unknown declaration type filling derives: ", declType)
	}
	if topMostCall || !dc.didShortCircuitOnRecursion {
		// Our completed result is only valid if it's either at top-level
		// (ensuring we've fully visited all child types in the recursive
		// substructure at least once) or if we performed no recursion-based
		// short-circuiting, in which case results are correct and absolute.
		//
		// Note that non-topMostCalls are invalid if we did short-circuit
		// on recursion, because we might have short-circuited just beneath
		// a type without having explored all of its children at least once
		// beneath it.
		//
		// For example, imagine A -> B -> C -> A.
		// When we start on A, we go to B, then go to C, then go to A, at which
		// point we short-circuit. The intermediate result for C is invalid
		// for anything except the computation of A and B above, as it does
		// not take into account that C contains A and B, only that it contains
		// its top-level fields (other than A). In order to get a correct
		// idea of the shape of C, we have to start with C, following through
		// every branch until we find C again.
		deriveStatus.complete = true
	}
	if topMostCall {
		// Reset intermediate state
		dc.topMostCall = true
		dc.didShortCircuitOnRecursion = false
	}
	deriveStatus.recursing = false
	dc.statuses[eci] = deriveStatus
	return derivesOut
}

func (dc *derivesCompiler) fillDerivesForType(ogType types.Type) derives {
	switch ogType.Kind {
	case types.ArrayType:
		if *ogType.ElementCount > 32 {
			// Turn off *all* derives for large arrays
			// FIXME(cramertj) remove when array derives are expanded
			return newDerives()
		} else {
			return dc.fillDerivesForType(*ogType.ElementType)
		}
	case types.VectorType:
		return derivesAll.remove(derivesCopy).and(dc.fillDerivesForType(*ogType.ElementType))
	case types.StringType:
		return derivesAll.remove(derivesCopy)
	case types.HandleType:
		fallthrough
	case types.RequestType:
		return derivesAll.remove(newDerives(derivesCopy, derivesClone))
	case types.PrimitiveType:
		switch ogType.PrimitiveSubtype {
		case types.Bool:
			fallthrough
		case types.Int8:
			fallthrough
		case types.Int16:
			fallthrough
		case types.Int32:
			fallthrough
		case types.Int64:
			fallthrough
		case types.Uint8:
			fallthrough
		case types.Uint16:
			fallthrough
		case types.Uint32:
			fallthrough
		case types.Uint64:
			return derivesAll
		case types.Float32:
			fallthrough
		case types.Float64:
			// Floats don't have a total ordering due to NAN and its multiple representations.
			return derivesAll.remove(newDerives(derivesEq, derivesOrd, derivesHash))
		}
	case types.IdentifierType:
		internalTypeDerives := dc.fillDerivesForECI(ogType.Identifier)
		if ogType.Nullable {
			// Nullable identifier types are put in an Option<Box<...>> and so aren't Copy
			return internalTypeDerives.remove(derivesCopy)
		} else {
			return internalTypeDerives
		}
	default:
		log.Panic("Unknown type kind in fillDerivesForType: ", ogType.Kind)
	}
	log.Panic("unreachable")
	return newDerives()
}

func Compile(r types.Root) Root {
	root := Root{}
	thisLibParsed := types.ParseLibraryName(r.Name)
	c := compiler{r.DeclsWithDependencies(), thisLibParsed, map[string]bool{}}

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

	for _, v := range r.Services {
		root.Services = append(root.Services, c.compileService(v))
	}

	for _, v := range r.Structs {
		root.Structs = append(root.Structs, c.compileStruct(v))
	}

	for _, v := range r.XUnions {
		root.XUnions = append(root.XUnions, c.compileXUnion(v))
	}

	// TODO(fxb/39159): Toggle to confirm Union & XUnion APIs align, and all
	// can be properly compiled.
	treatUnionAsXUnions := false
	for _, v := range r.Unions {
		if !treatUnionAsXUnions {
			if v.Attributes.HasAttribute("Result") {
				root.Results = append(root.Results, c.compileResultFromUnion(v, root))
			} else {
				root.Unions = append(root.Unions, c.compileUnion(v))
			}
		} else {
			vConverted := types.ConvertUnionToXUnion(v)
			if v.Attributes.HasAttribute("Result") {
				root.Results = append(root.Results, c.compileResultFromXUnion(vConverted, root))
			} else {
				root.XUnions = append(root.XUnions, c.compileXUnion(vConverted))
			}
		}
	}

	for _, v := range r.Tables {
		root.Tables = append(root.Tables, c.compileTable(v))
	}

	c.fillDerives(&root)

	thisLibCompiled := compileLibraryName(thisLibParsed)

	// Sort the extern crates to make sure the generated file is
	// consistent across builds.
	var externCrates []string
	for k := range c.externCrates {
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
