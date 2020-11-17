// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"sort"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type EncodedCompoundIdentifier = fidl.EncodedCompoundIdentifier

type Type struct {
	Decl     string
	DeclType fidl.DeclType
	Derives  derives
}

type Bits struct {
	fidl.Bits
	Name    string
	Type    string
	Members []BitsMember
}

type BitsMember struct {
	fidl.BitsMember
	Name  string
	Value string
}

type Const struct {
	fidl.Attributes
	Name  string
	Type  string
	Value string
}

type Enum struct {
	fidl.Enum
	Name    string
	Type    string
	Members []EnumMember
}

type EnumMember struct {
	fidl.EnumMember
	Name  string
	Value string
}

type Union struct {
	fidl.Union
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []UnionMember
}

type UnionMember struct {
	fidl.Attributes
	Type              string
	OGType            fidl.Type
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type ResultOkEntry struct {
	OGType            fidl.Type
	Type              string
	HasHandleMetadata bool
	HandleWrapperName string
}

type Result struct {
	fidl.Attributes
	ECI       EncodedCompoundIdentifier
	Derives   derives
	Name      string
	Ok        []ResultOkEntry
	ErrOGType fidl.Type
	ErrType   string
	Size      int
	Alignment int
}

type Struct struct {
	fidl.Attributes
	ECI                     EncodedCompoundIdentifier
	Derives                 derives
	Name                    string
	Members                 []StructMember
	PaddingMarkers          []PaddingMarker
	FlattenedPaddingMarkers []PaddingMarker
	// Store size and alignment for Old and V1 versions of the wire format. The
	// numbers will be different if the struct contains a union within it. Only
	// structs have this information because fidl::encoding only uses these
	// precalculated numbers for structs and unions.
	Size       int
	Alignment  int
	HasPadding bool
	// True iff the fidl_struct_copy! macro should be used instead of fidl_struct!.
	UseFidlStructCopy bool
}

type StructMember struct {
	fidl.Attributes
	OGType            fidl.Type
	Type              string
	Name              string
	Offset            int
	HasDefault        bool
	DefaultValue      string
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type PaddingMarker struct {
	Type   string
	Offset int
	// Mask is a string so it can be in hex.
	Mask string
}

type Table struct {
	fidl.Table
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []TableMember
}

type TableMember struct {
	fidl.Attributes
	OGType            fidl.Type
	Type              string
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type Protocol struct {
	fidl.Attributes
	ECI         EncodedCompoundIdentifier
	Name        string
	Methods     []Method
	ServiceName string
}

type Method struct {
	fidl.Attributes
	Ordinal        uint64
	Name           string
	CamelName      string
	HasRequest     bool
	Request        []Parameter
	HasResponse    bool
	Response       []Parameter
	Result         *Result
	IsTransitional bool
}

type Parameter struct {
	OGType            fidl.Type
	Type              string
	BorrowedType      string
	Name              string
	HandleWrapperName string
	HasHandleMetadata bool
}

type HandleMetadataWrapper struct {
	Name    string
	Subtype string
	Rights  string
}

type Service struct {
	fidl.Attributes
	Name        string
	Members     []ServiceMember
	ServiceName string
}

type ServiceMember struct {
	fidl.Attributes
	ProtocolType string
	Name         string
	CamelName    string
	SnakeName    string
}

type Root struct {
	ExternCrates           []string
	Bits                   []Bits
	Consts                 []Const
	Enums                  []Enum
	Structs                []Struct
	Unions                 []Union
	Results                []Result
	Tables                 []Table
	Protocols              []Protocol
	Services               []Service
	HandleMetadataWrappers []HandleMetadataWrapper
}

func (r *Root) findProtocol(eci EncodedCompoundIdentifier) *Protocol {
	for i := range r.Protocols {
		if r.Protocols[i].ECI == eci {
			return &r.Protocols[i]
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

func (r *Root) findResult(eci EncodedCompoundIdentifier) *Result {
	for i := range r.Results {
		if r.Results[i].ECI == eci {
			return &r.Results[i]
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

var reservedWords = map[string]struct{}{
	"as":       {},
	"box":      {},
	"break":    {},
	"const":    {},
	"continue": {},
	"crate":    {},
	"else":     {},
	"enum":     {},
	"extern":   {},
	"false":    {},
	"fn":       {},
	"for":      {},
	"if":       {},
	"impl":     {},
	"in":       {},
	"let":      {},
	"loop":     {},
	"match":    {},
	"mod":      {},
	"move":     {},
	"mut":      {},
	"pub":      {},
	"ref":      {},
	"return":   {},
	"self":     {},
	"Self":     {},
	"static":   {},
	"struct":   {},
	"super":    {},
	"trait":    {},
	"true":     {},
	"type":     {},
	"unsafe":   {},
	"use":      {},
	"where":    {},
	"while":    {},

	// Keywords reserved for future use (future-proofing...)
	"abstract": {},
	"alignof":  {},
	"await":    {},
	"become":   {},
	"do":       {},
	"final":    {},
	"macro":    {},
	"offsetof": {},
	"override": {},
	"priv":     {},
	"proc":     {},
	"pure":     {},
	"sizeof":   {},
	"typeof":   {},
	"unsized":  {},
	"virtual":  {},
	"yield":    {},

	// Weak keywords (special meaning in specific contexts)
	// These are ok in all contexts of fidl names.
	//"default":	{},
	//"union":	{},

	// Things that are not keywords, but for which collisions would be very unpleasant
	"Result":  {},
	"Ok":      {},
	"Err":     {},
	"Vec":     {},
	"Option":  {},
	"Some":    {},
	"None":    {},
	"Box":     {},
	"Future":  {},
	"Stream":  {},
	"Never":   {},
	"Send":    {},
	"fidl":    {},
	"futures": {},
	"zx":      {},
	"async":   {},
	"on_open": {},
	"OnOpen":  {},
}

var reservedSuffixes = []string{
	"Impl",
	"Marker",
	"Proxy",
	"ProxyProtocol",
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

func changeIfReserved(val fidl.Identifier) string {
	str := string(val)
	if hasReservedSuffix(str) || isReservedWord(str) {
		return str + "_"
	}
	return str
}

var primitiveTypes = map[fidl.PrimitiveSubtype]string{
	fidl.Bool:    "bool",
	fidl.Int8:    "i8",
	fidl.Int16:   "i16",
	fidl.Int32:   "i32",
	fidl.Int64:   "i64",
	fidl.Uint8:   "u8",
	fidl.Uint16:  "u16",
	fidl.Uint32:  "u32",
	fidl.Uint64:  "u64",
	fidl.Float32: "f32",
	fidl.Float64: "f64",
}

var handleSubtypes = map[fidl.HandleSubtype]string{
	fidl.Bti:          "Bti",
	fidl.Channel:      "Channel",
	fidl.Clock:        "Clock",
	fidl.DebugLog:     "DebugLog",
	fidl.Event:        "Event",
	fidl.Eventpair:    "EventPair",
	fidl.Exception:    "Exception",
	fidl.Fifo:         "Fifo",
	fidl.Guest:        "Guest",
	fidl.Handle:       "Handle",
	fidl.Interrupt:    "Interrupt",
	fidl.Iommu:        "Iommu",
	fidl.Job:          "Job",
	fidl.Pager:        "Pager",
	fidl.PciDevice:    "PciDevice",
	fidl.Pmt:          "Pmt",
	fidl.Port:         "Port",
	fidl.Process:      "Process",
	fidl.Profile:      "Profile",
	fidl.Resource:     "Resource",
	fidl.Socket:       "Socket",
	fidl.Stream:       "Stream",
	fidl.SuspendToken: "SuspendToken",
	fidl.Thread:       "Thread",
	fidl.Time:         "Timer",
	fidl.Vcpu:         "Vcpu",
	fidl.Vmar:         "Vmar",
	fidl.Vmo:          "Vmo",
}

var handleSubtypeConsts = map[fidl.HandleSubtype]string{
	fidl.Bti:          "BTI",
	fidl.Channel:      "CHANNEL",
	fidl.Clock:        "CLOCK",
	fidl.DebugLog:     "LOG",
	fidl.Event:        "EVENT",
	fidl.Eventpair:    "EVENTPAIR",
	fidl.Exception:    "EXCEPTION",
	fidl.Fifo:         "FIFO",
	fidl.Guest:        "GUEST",
	fidl.Handle:       "NONE",
	fidl.Interrupt:    "INTERRUPT",
	fidl.Iommu:        "IOMMU",
	fidl.Job:          "JOB",
	fidl.Pager:        "PAGER",
	fidl.PciDevice:    "PCI_DEVICE",
	fidl.Pmt:          "PMT",
	fidl.Port:         "PORT",
	fidl.Process:      "PROCESS",
	fidl.Profile:      "PROFILE",
	fidl.Resource:     "RESOURCE",
	fidl.Socket:       "SOCKET",
	fidl.Stream:       "STREAM",
	fidl.SuspendToken: "SUSPEND_TOKEN",
	fidl.Thread:       "THREAD",
	fidl.Time:         "TIMER",
	fidl.Vcpu:         "VCPU",
	fidl.Vmar:         "VMAR",
	fidl.Vmo:          "VMO",
}

type compiler struct {
	decls                  fidl.DeclMap
	library                fidl.LibraryIdentifier
	externCrates           map[string]struct{}
	requestResponsePayload map[fidl.EncodedCompoundIdentifier]fidl.Struct
	structs                map[fidl.EncodedCompoundIdentifier]fidl.Struct
	results                map[fidl.EncodedCompoundIdentifier]Result
	handleMetadataWrappers map[string]HandleMetadataWrapper
}

func (c *compiler) inExternalLibrary(ci fidl.CompoundIdentifier) bool {
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

func compileCamelIdentifier(val fidl.Identifier) string {
	return fidl.ToUpperCamelCase(changeIfReserved(val))
}

func compileLibraryName(library fidl.LibraryIdentifier) string {
	parts := []string{"fidl"}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(fidl.Identifier(strings.Join(parts, "_")))
}

func compileSnakeIdentifier(val fidl.Identifier) string {
	return fidl.ToSnakeCase(changeIfReserved(val))
}

func compileScreamingSnakeIdentifier(val fidl.Identifier) string {
	return fidl.ConstNameToAllCapsSnake(changeIfReserved(val))
}

func (c *compiler) compileCompoundIdentifier(val fidl.CompoundIdentifier) string {
	strs := []string{}
	if c.inExternalLibrary(val) {
		externName := compileLibraryName(val.Library)
		c.externCrates[externName] = struct{}{}
		strs = append(strs, externName)
	}
	str := changeIfReserved(val.Name)
	strs = append(strs, str)
	if val.Member != "" {
		strs = append(strs, string(val.Member))
	}
	return strings.Join(strs, "::")
}

func (c *compiler) compileCamelCompoundIdentifier(eci fidl.EncodedCompoundIdentifier) string {
	val := fidl.ParseCompoundIdentifier(eci)
	val.Name = fidl.Identifier(compileCamelIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileSnakeCompoundIdentifier(eci fidl.EncodedCompoundIdentifier) string {
	val := fidl.ParseCompoundIdentifier(eci)
	val.Name = fidl.Identifier(compileSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileScreamingSnakeCompoundIdentifier(eci fidl.EncodedCompoundIdentifier) string {
	val := fidl.ParseCompoundIdentifier(eci)
	val.Name = fidl.Identifier(compileScreamingSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func compileLiteral(val fidl.Literal, typ fidl.Type) string {
	switch val.Kind {
	case fidl.StringLiteral:
		return fmt.Sprintf("r###\"%s\"###", val.Value)
	case fidl.NumericLiteral:
		if typ.Kind == fidl.PrimitiveType &&
			(typ.PrimitiveSubtype == fidl.Float32 || typ.PrimitiveSubtype == fidl.Float64) {
			if !strings.ContainsRune(val.Value, '.') {
				return fmt.Sprintf("%s.0", val.Value)
			}
			return val.Value
		}
		return val.Value
	case fidl.TrueLiteral:
		return "true"
	case fidl.FalseLiteral:
		return "false"
	case fidl.DefaultLiteral:
		return "::Default::default()"
	default:
		panic(fmt.Sprintf("unknown literal kind: %v", val.Kind))
	}
}

func (c *compiler) compileConstant(val fidl.Constant, typ fidl.Type) string {
	switch val.Kind {
	case fidl.IdentifierConstant:
		parts := fidl.ParseCompoundIdentifier(val.Identifier)
		if parts.Member == fidl.Identifier("") {
			// Top-level constant.
			parts.Name = fidl.Identifier(compileScreamingSnakeIdentifier(parts.Name))
		} else {
			// Bits or enum member.
			parts.Name = fidl.Identifier(compileCamelIdentifier(parts.Name))
			// TODO(fxbug.dev/47034) For bits the member should be SCREAMING_SNAKE_CASE.
			parts.Member = fidl.Identifier(compileCamelIdentifier(parts.Member))
		}
		return c.compileCompoundIdentifier(parts)
	case fidl.LiteralConstant:
		return compileLiteral(val.Literal, typ)
	default:
		panic(fmt.Sprintf("unknown constant kind: %v", val.Kind))
	}
}

func (c *compiler) compileConst(val fidl.Const) Const {
	name := c.compileScreamingSnakeCompoundIdentifier(val.Name)
	var r Const
	if val.Type.Kind == fidl.StringType {
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

func compilePrimitiveSubtype(val fidl.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

func compileHandleSubtype(val fidl.HandleSubtype) string {
	if t, ok := handleSubtypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown handle type: %v", val))
}

type FieldHandleInformation struct {
	fullObjectType    string
	fullRights        string
	shortObjectType   string
	shortRights       string
	hasHandleMetadata bool
}

func (c *compiler) fieldHandleInformation(val *fidl.Type) FieldHandleInformation {
	if val.ElementType != nil {
		return c.fieldHandleInformation(val.ElementType)
	}
	if val.Kind == fidl.RequestType {
		return FieldHandleInformation{
			fullObjectType:    "fidl::ObjectType::CHANNEL",
			fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
			shortObjectType:   "CHANNEL",
			shortRights:       "CHANNEL_DEFAULT",
			hasHandleMetadata: true,
		}
	}
	if val.Kind == fidl.IdentifierType {
		declType, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		if declType == fidl.ProtocolDeclType {
			return FieldHandleInformation{
				fullObjectType:    "fidl::ObjectType::CHANNEL",
				fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
				shortObjectType:   "CHANNEL",
				shortRights:       "CHANNEL_DEFAULT",
				hasHandleMetadata: true,
			}
		}
	}
	if val.Kind != fidl.HandleType {
		return FieldHandleInformation{
			fullObjectType:    "fidl::ObjectType::NONE",
			fullRights:        "fidl::Rights::NONE",
			shortObjectType:   "NONE",
			shortRights:       "NONE",
			hasHandleMetadata: false,
		}
	}
	subtype, ok := handleSubtypeConsts[val.HandleSubtype]
	if !ok {
		panic(fmt.Sprintf("unknown handle type for const: %v", val))
	}
	return FieldHandleInformation{
		fullObjectType:    fmt.Sprintf("fidl::ObjectType::%s", subtype),
		fullRights:        fmt.Sprintf("fidl::Rights::from_bits_const(%d).unwrap()", val.HandleRights),
		shortObjectType:   subtype,
		shortRights:       fmt.Sprintf("%d", val.HandleRights),
		hasHandleMetadata: true,
	}
}

func (c *compiler) compileType(val fidl.Type, borrowed bool) Type {
	var r string
	var declType fidl.DeclType
	switch val.Kind {
	case fidl.ArrayType:
		t := c.compileType(*val.ElementType, borrowed)
		r = fmt.Sprintf("[%s; %v]", t.Decl, *val.ElementCount)
		if borrowed {
			r = fmt.Sprintf("&mut %s", r)
		}
	case fidl.VectorType:
		t := c.compileType(*val.ElementType, borrowed)
		var inner string
		if borrowed {
			// We use slices for primitive numeric types so that
			// encoding becomes a memcpy. Rust does not guarantee
			// the bit patterns for bool values, so we omit them
			// from the optimization.
			if val.ElementType.Kind == fidl.PrimitiveType && val.ElementType.PrimitiveSubtype != fidl.Bool {
				inner = fmt.Sprintf("&[%s]", t.Decl)
			} else {
				inner = fmt.Sprintf("&mut dyn ExactSizeIterator<Item = %s>", t.Decl)
			}
		} else {
			inner = fmt.Sprintf("Vec<%s>", t.Decl)
		}
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", inner)
		} else {
			r = inner
		}
	case fidl.StringType:
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
	case fidl.HandleType:
		r = fmt.Sprintf("fidl::%s", compileHandleSubtype(val.HandleSubtype))
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", r)
		}
	case fidl.RequestType:
		r = c.compileCamelCompoundIdentifier(val.RequestSubtype)
		r = fmt.Sprintf("fidl::endpoints::ServerEnd<%sMarker>", r)
		if val.Nullable {
			r = fmt.Sprintf("Option<%s>", r)
		}
	case fidl.PrimitiveType:
		// Primitive types are small, simple, and never contain handles,
		// so there's no need to borrow them
		r = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case fidl.IdentifierType:
		t := c.compileCamelCompoundIdentifier(val.Identifier)
		declType, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		switch declType {
		case fidl.BitsDeclType, fidl.EnumDeclType:
			// Bits and enums are small, simple, and never contain handles,
			// so no need to borrow
			borrowed = false
			fallthrough
		case fidl.ConstDeclType, fidl.StructDeclType, fidl.UnionDeclType:
			if val.Nullable {
				if borrowed {
					r = fmt.Sprintf("Option<&mut %s>", t)
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
		case fidl.TableDeclType:
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", t)
			} else {
				// TODO(fxbug.dev/42304): Replace with "&mut %s".
				r = t
			}
		case fidl.ProtocolDeclType:
			r = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
			if val.Nullable {
				r = fmt.Sprintf("Option<%s>", r)
			}
		default:
			panic(fmt.Sprintf("unknown declaration type: %v", declType))
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}

	return Type{
		Decl:     r,
		DeclType: declType,
	}
}

func (c *compiler) compileBits(val fidl.Bits) Bits {
	e := Bits{
		Bits:    val,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Type:    c.compileType(val.Type, false).Decl,
		Members: []BitsMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, BitsMember{
			BitsMember: v,
			// TODO(fxbug.dev/47034) Should be SCREAMING_SNAKE_CASE.
			Name:  compileCamelIdentifier(v.Name),
			Value: c.compileConstant(v.Value, val.Type),
		})
	}
	return e
}

func (c *compiler) compileEnum(val fidl.Enum) Enum {
	e := Enum{
		Enum:    val,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Type:    compilePrimitiveSubtype(val.Type),
		Members: []EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			EnumMember: v,
			Name:       compileCamelIdentifier(v.Name),
			// TODO(fxbug.dev/7660): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, fidl.Type{
				Kind:             fidl.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
		})
	}
	return e
}

func (c *compiler) compileHandleMetadataWrapper(val *fidl.Type) (string, bool) {
	hi := c.fieldHandleInformation(val)
	name := fmt.Sprintf("HandleWrapperObjectType%sRights%s", hi.shortObjectType, hi.shortRights)
	wrapper := HandleMetadataWrapper{
		Name:    name,
		Subtype: hi.fullObjectType,
		Rights:  hi.fullRights,
	}
	if hi.hasHandleMetadata {
		if _, ok := c.handleMetadataWrappers[name]; !ok {
			c.handleMetadataWrappers[name] = wrapper
		}
	}
	return name, hi.hasHandleMetadata
}

func (c *compiler) compileParameterArray(payload fidl.EncodedCompoundIdentifier) []Parameter {
	val, ok := c.requestResponsePayload[payload]
	if !ok {
		panic(fmt.Sprintf("unknown request/response struct: %v", payload))
	}

	var parameters []Parameter
	for _, v := range val.Members {
		wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&v.Type)
		parameters = append(parameters, Parameter{
			OGType:            v.Type,
			Type:              c.compileType(v.Type, false).Decl,
			BorrowedType:      c.compileType(v.Type, true).Decl,
			Name:              compileSnakeIdentifier(v.Name),
			HandleWrapperName: wrapperName,
			HasHandleMetadata: hasHandleMetadata,
		})
	}
	return parameters
}

func (c *compiler) compileProtocol(val fidl.Protocol) Protocol {
	r := Protocol{
		Attributes:  val.Attributes,
		ECI:         val.Name,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Methods:     []Method{},
		ServiceName: strings.Trim(val.GetServiceName(), "\""),
	}

	for _, v := range val.Methods {
		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		var foundResult *Result
		if len(v.Response) == 1 && v.Response[0].Type.Kind == fidl.IdentifierType {
			responseType := v.Response[0].Type
			if result, ok := c.results[responseType.Identifier]; ok {
				foundResult = &result
			}
		}
		m := Method{
			Attributes:     v.Attributes,
			Ordinal:        v.Ordinal,
			Name:           name,
			CamelName:      camelName,
			HasRequest:     v.HasRequest,
			HasResponse:    v.HasResponse,
			Result:         foundResult,
			IsTransitional: v.IsTransitional(),
		}
		if v.RequestPayload != "" {
			m.Request = c.compileParameterArray(v.RequestPayload)
		}
		if v.ResponsePayload != "" {
			m.Response = c.compileParameterArray(v.ResponsePayload)
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileService(val fidl.Service) Service {
	r := Service{
		Attributes:  val.Attributes,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Members:     []ServiceMember{},
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		m := ServiceMember{
			Attributes:   v.Attributes,
			Name:         string(v.Name),
			CamelName:    compileCamelIdentifier(v.Name),
			SnakeName:    compileSnakeIdentifier(v.Name),
			ProtocolType: c.compileCamelCompoundIdentifier(v.Type.Identifier),
		}
		r.Members = append(r.Members, m)
	}

	return r
}

func (c *compiler) compileStructMember(val fidl.StructMember) StructMember {
	memberType := c.compileType(val.Type, false)
	hi := c.fieldHandleInformation(&val.Type)
	return StructMember{
		Attributes:        val.Attributes,
		Type:              memberType.Decl,
		OGType:            val.Type,
		Name:              compileSnakeIdentifier(val.Name),
		Offset:            val.FieldShapeV1.Offset,
		HasDefault:        false,
		DefaultValue:      "", // TODO(cramertj) support defaults
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func (c *compiler) populateFullStructMaskForStruct(mask []byte, val fidl.Struct, flatten bool) {
	paddingEnd := val.TypeShapeV1.InlineSize - 1
	for i := len(val.Members) - 1; i >= 0; i-- {
		member := val.Members[i]
		if flatten {
			c.populateFullStructMaskForType(mask[member.FieldShapeV1.Offset:paddingEnd+1], &member.Type, flatten)
		}
		for j := 0; j < member.FieldShapeV1.Padding; j++ {
			mask[paddingEnd-j] = 0xff
		}
		paddingEnd = member.FieldShapeV1.Offset - 1
	}
}

func (c *compiler) populateFullStructMaskForType(mask []byte, typ *fidl.Type, flatten bool) {
	if typ.Nullable {
		return
	}
	switch typ.Kind {
	case fidl.ArrayType:
		elemByteSize := len(mask) / *typ.ElementCount
		for i := 0; i < *typ.ElementCount; i++ {
			c.populateFullStructMaskForType(mask[i*elemByteSize:(i+1)*elemByteSize], typ.ElementType, flatten)
		}
	case fidl.IdentifierType:
		if c.inExternalLibrary(fidl.ParseCompoundIdentifier(typ.Identifier)) {
			// This behavior is matched by computeUseFullStructCopy.
			return
		}
		declType := c.decls[typ.Identifier]
		if declType == fidl.StructDeclType {
			st, ok := c.structs[typ.Identifier]
			if !ok {
				panic(fmt.Sprintf("struct not found: %v", typ.Identifier))
			}
			c.populateFullStructMaskForStruct(mask, st, flatten)
		}
	}
}

func (c *compiler) buildPaddingMarkers(val fidl.Struct, flatten bool) []PaddingMarker {
	var paddingMarkers []PaddingMarker

	// Construct a mask across the whole struct with 0xff bytes where there is padding.
	fullStructMask := make([]byte, val.TypeShapeV1.InlineSize)
	c.populateFullStructMaskForStruct(fullStructMask, val, flatten)

	// Split up the mask into aligned integer mask segments that can be outputted in the
	// fidl_struct! macro.
	// Only the sections needing padding are outputted.
	// e.g. 00ffff0000ffff000000000000000000 -> 00ffff0000ffff00, 0000000000000000
	//                                       -> []PaddingMarker{"u64", 0, "0x00ffff0000ffff00u64"}
	extractNonzeroSliceOffsets := func(stride int) []int {
		var offsets []int
		for endi := stride - 1; endi < len(fullStructMask); endi += stride {
			i := endi - (stride - 1)
			if bytes.Contains(fullStructMask[i:i+stride], []byte{0xff}) {
				offsets = append(offsets, i)
			}
		}
		return offsets
	}
	zeroSlice := func(s []byte) {
		for i := range s {
			s[i] = 0
		}
	}
	for _, i := range extractNonzeroSliceOffsets(8) {
		s := fullStructMask[i : i+8]
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Type:   "u64",
			Offset: i,
			Mask:   fmt.Sprintf("0x%016xu64", binary.LittleEndian.Uint64(s)),
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	for _, i := range extractNonzeroSliceOffsets(4) {
		s := fullStructMask[i : i+4]
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Type:   "u32",
			Offset: i,
			Mask:   fmt.Sprintf("0x%08xu32", binary.LittleEndian.Uint32(s)),
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	for _, i := range extractNonzeroSliceOffsets(2) {
		s := fullStructMask[i : i+2]
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Type:   "u16",
			Offset: i,
			Mask:   fmt.Sprintf("0x%04xu16", binary.LittleEndian.Uint16(s)),
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	if bytes.Contains(fullStructMask, []byte{0xff}) {
		// This shouldn't be possible because it requires an alignment 1 struct to have padding.
		panic(fmt.Sprintf("expected mask to be zero, was %v", fullStructMask))
	}
	return paddingMarkers
}

func (c *compiler) computeUseFidlStructCopyForStruct(st fidl.Struct) bool {
	for _, member := range st.Members {
		if !c.computeUseFidlStructCopy(&member.Type) {
			return false
		}
	}
	return true
}

func (c *compiler) computeUseFidlStructCopy(typ *fidl.Type) bool {
	if typ.Nullable {
		return false
	}
	switch typ.Kind {
	case fidl.ArrayType:
		return c.computeUseFidlStructCopy(typ.ElementType)
	case fidl.VectorType:
		return false
	case fidl.StringType:
		return false
	case fidl.HandleType:
		return false
	case fidl.RequestType:
		return false
	case fidl.PrimitiveType:
		switch typ.PrimitiveSubtype {
		case fidl.Bool, fidl.Float32, fidl.Float64:
			return false
		}
		return true
	case fidl.IdentifierType:
		if c.inExternalLibrary(fidl.ParseCompoundIdentifier(typ.Identifier)) {
			return false
		}
		declType := c.decls[typ.Identifier]
		switch declType {
		case fidl.BitsDeclType:
			return false
		case fidl.EnumDeclType:
			return false
		case fidl.ProtocolDeclType:
			return false
		case fidl.StructDeclType:
			st, ok := c.structs[typ.Identifier]
			if !ok {
				panic(fmt.Sprintf("struct not found: %v", typ.Identifier))
			}
			return c.computeUseFidlStructCopyForStruct(st)
		case fidl.UnionDeclType:
			return false
		case fidl.TableDeclType:
			return false
		default:
			panic(fmt.Sprintf("unknown declaration type: %v", declType))
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", typ.Kind))
	}
}

func (c *compiler) compileStruct(val fidl.Struct) Struct {
	name := c.compileCamelCompoundIdentifier(val.Name)
	r := Struct{
		Attributes:              val.Attributes,
		ECI:                     val.Name,
		Name:                    name,
		Members:                 []StructMember{},
		Size:                    val.TypeShapeV1.InlineSize,
		Alignment:               val.TypeShapeV1.Alignment,
		PaddingMarkers:          c.buildPaddingMarkers(val, false),
		FlattenedPaddingMarkers: c.buildPaddingMarkers(val, true),
	}

	for _, v := range val.Members {
		member := c.compileStructMember(v)
		r.Members = append(r.Members, member)
		r.HasPadding = r.HasPadding || (v.FieldShapeV1.Padding != 0)
	}

	r.UseFidlStructCopy = c.computeUseFidlStructCopyForStruct(val)

	return r
}

func (c *compiler) compileUnionMember(val fidl.UnionMember) UnionMember {
	hi := c.fieldHandleInformation(&val.Type)
	return UnionMember{
		Attributes:        val.Attributes,
		Type:              c.compileType(val.Type, false).Decl,
		OGType:            val.Type,
		Name:              compileCamelIdentifier(val.Name),
		Ordinal:           val.Ordinal,
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func (c *compiler) compileUnion(val fidl.Union) Union {
	r := Union{
		Union:   val,
		ECI:     val.Name,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Members: []UnionMember{},
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func (c *compiler) compileResultFromUnion(val fidl.Union, root Root) Result {
	r := Result{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Ok:         []ResultOkEntry{},
		ErrOGType:  val.Members[1].Type,
		ErrType:    c.compileUnionMember(val.Members[1]).Type,
		Size:       val.TypeShapeV1.InlineSize,
		Alignment:  val.TypeShapeV1.Alignment,
	}

	OkArm := val.Members[0]
	ci := c.compileCamelCompoundIdentifier(OkArm.Type.Identifier)

	// always a struct on the Ok arms in Results
	for _, v := range root.Structs {
		if v.Name == ci {
			for _, m := range v.Members {
				wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&m.OGType)
				r.Ok = append(r.Ok, ResultOkEntry{
					OGType:            m.OGType,
					Type:              m.Type,
					HasHandleMetadata: hasHandleMetadata,
					HandleWrapperName: wrapperName,
				})
			}
		}
	}

	c.results[r.ECI] = r

	return r
}

func (c *compiler) compileTable(table fidl.Table) Table {
	var members []TableMember
	for _, member := range table.SortedMembersNoReserved() {
		hi := c.fieldHandleInformation(&member.Type)
		members = append(members, TableMember{
			Attributes:        member.Attributes,
			OGType:            member.Type,
			Type:              c.compileType(member.Type, false).Decl,
			Name:              compileSnakeIdentifier(member.Name),
			Ordinal:           member.Ordinal,
			HasHandleMetadata: hi.hasHandleMetadata,
			HandleSubtype:     hi.fullObjectType,
			HandleRights:      hi.fullRights,
		})
	}
	return Table{
		Table:   table,
		ECI:     table.Name,
		Name:    c.compileCamelCompoundIdentifier(table.Name),
		Members: members,
	}
}

type derives uint16

const (
	derivesDebug derives = 1 << iota
	derivesCopy
	derivesClone
	derivesEq
	derivesPartialEq
	derivesOrd
	derivesPartialOrd
	derivesHash
	derivesAsBytes
	derivesFromBytes
	derivesAll derives = (1 << iota) - 1

	derivesMinimal            derives = derivesDebug | derivesPartialEq
	derivesHashMap            derives = derivesDebug | derivesClone | derivesEq | derivesPartialEq
	derivesMinimalNonResource derives = derivesMinimal | derivesClone
	derivesAllButZerocopy     derives = derivesAll & ^derivesAsBytes & ^derivesFromBytes
)

// note: keep this list in the same order as the derives definitions
var derivesNames = []string{
	// [START derived_traits]
	"Debug",
	"Copy",
	"Clone",
	"Eq",
	"PartialEq",
	"Ord",
	"PartialOrd",
	"Hash",
	"zerocopy::AsBytes",
	"zerocopy::FromBytes",
	// [END derived_traits]
}

func (v derives) and(others derives) derives {
	return v & others
}

func (v derives) remove(others ...derives) derives {
	result := v
	for _, other := range others {
		result &= ^other
	}
	return result
}

func (v derives) andUnknown() derives {
	return v.and(derivesMinimal)
}

func (v derives) andUnknownNonResource() derives {
	return v.and(derivesMinimalNonResource)
}

func (v derives) contains(other derives) bool {
	return (v & other) != 0
}

func (v derives) String() string {
	var parts []string
	for i, bit := 0, derives(1); bit&derivesAll != 0; i, bit = i+1, bit<<1 {
		if v.contains(bit) {
			parts = append(parts, derivesNames[i])
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

// [START fill_derives]
// Calculates what traits should be derived for each output type,
// filling in all `*derives` in the IR.
func (c *compiler) fillDerives(ir *Root) {
	// [END fill_derives]
	dc := &derivesCompiler{
		compiler:                   c,
		topMostCall:                true,
		didShortCircuitOnRecursion: false,
		statuses:                   make(map[EncodedCompoundIdentifier]deriveStatus),
		root:                       ir,
	}

	// Bits and enums always derive all traits
	for _, v := range ir.Protocols {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Structs {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Unions {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Results {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Tables {
		dc.fillDerivesForECI(v.ECI)
	}
}

func (dc *derivesCompiler) fillDerivesForECI(eci EncodedCompoundIdentifier) derives {
	if dc.inExternalLibrary(fidl.ParseCompoundIdentifier(eci)) {
		// We must be conservative with external types and assume they only
		// derive the minimal set of traits that all types derive.
		// TODO(fxbug.dev/61760): Make external type information available here.
		return derivesMinimal
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
	case fidl.ConstDeclType:
		panic("const decl should never have derives")
	case fidl.BitsDeclType, fidl.EnumDeclType:
		// Enums and bits are always simple, non-float primitives which
		// implement all derivable traits except zerocopy.
		derivesOut = derivesAllButZerocopy
	case fidl.ProtocolDeclType:
		// When a protocol is used as a type, it means a ClientEnd in Rust.
		derivesOut = derivesAllButZerocopy.remove(derivesCopy, derivesClone)
	case fidl.StructDeclType:
		st := dc.root.findStruct(eci)
		if st == nil {
			panic(fmt.Sprintf("struct not found: %v", eci))
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
		if st.HasPadding {
			derivesOut = derivesOut.remove(derivesAsBytes, derivesFromBytes)
		}
		st.Derives = derivesOut
	case fidl.TableDeclType:
		table := dc.root.findTable(eci)
		if table == nil {
			panic(fmt.Sprintf("table not found: %v", eci))
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = table.Derives
			break typeSwitch
		}
		derivesOut = derivesAllButZerocopy
		for _, member := range table.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		if table.IsResourceType() {
			derivesOut = derivesOut.andUnknown()
		} else {
			derivesOut = derivesOut.andUnknownNonResource()
		}
		derivesOut = derivesOut.and(derivesHashMap)
		table.Derives = derivesOut
	case fidl.UnionDeclType:
		union := dc.root.findUnion(eci)
		var result *Result
		if union == nil {
			result = dc.root.findResult(eci)
		}
		if union == nil && result == nil {
			panic(fmt.Sprintf("union not found: %v", eci))
		}
		if union != nil {
			// It's a union, not a result
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = union.Derives
				break typeSwitch
			}
			derivesOut = derivesAllButZerocopy
			for _, member := range union.Members {
				derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
			}
			if union.Strictness.IsFlexible() {
				if union.IsResourceType() {
					derivesOut = derivesOut.andUnknown()
				} else {
					derivesOut = derivesOut.andUnknownNonResource()
				}
			}
			union.Derives = derivesOut
		} else {
			// It's a Result, not a union
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = result.Derives
				break typeSwitch
			}
			derivesOut = derivesAllButZerocopy
			for _, ok := range result.Ok {
				derivesOut = derivesOut.and(dc.fillDerivesForType(ok.OGType))
			}
			derivesOut = derivesOut.and(dc.fillDerivesForType(result.ErrOGType))
			result.Derives = derivesOut
		}
	default:
		panic(fmt.Sprintf("unknown declaration type: %v", declType))
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

func (dc *derivesCompiler) fillDerivesForType(ogType fidl.Type) derives {
	switch ogType.Kind {
	case fidl.ArrayType:
		return dc.fillDerivesForType(*ogType.ElementType)
	case fidl.VectorType:
		return derivesAllButZerocopy.remove(derivesCopy).and(dc.fillDerivesForType(*ogType.ElementType))
	case fidl.StringType:
		return derivesAllButZerocopy.remove(derivesCopy)
	case fidl.HandleType, fidl.RequestType:
		return derivesAllButZerocopy.remove(derivesCopy, derivesClone)
	case fidl.PrimitiveType:
		switch ogType.PrimitiveSubtype {
		case fidl.Bool:
			return derivesAllButZerocopy
		case fidl.Int8, fidl.Int16, fidl.Int32, fidl.Int64:
			return derivesAll
		case fidl.Uint8, fidl.Uint16, fidl.Uint32, fidl.Uint64:
			return derivesAll
		case fidl.Float32, fidl.Float64:
			// Floats don't have a total ordering due to NAN and its multiple representations.
			return derivesAllButZerocopy.remove(derivesEq, derivesOrd, derivesHash)
		default:
			panic(fmt.Sprintf("unknown primitive type: %v", ogType.PrimitiveSubtype))
		}
	case fidl.IdentifierType:
		internalTypeDerives := dc.fillDerivesForECI(ogType.Identifier)
		if ogType.Nullable {
			// Nullable identifier types are put in an Option<Box<...>> and so aren't Copy
			return internalTypeDerives.remove(derivesCopy, derivesAsBytes, derivesFromBytes)
		}
		return internalTypeDerives
	default:
		panic(fmt.Sprintf("unknown type kind: %v", ogType.Kind))
	}
}

func Compile(r fidl.Root) Root {
	r = r.ForBindings("rust")
	root := Root{}
	thisLibParsed := fidl.ParseLibraryName(r.Name)
	c := compiler{
		r.DeclsWithDependencies(),
		thisLibParsed,
		map[string]struct{}{},
		map[fidl.EncodedCompoundIdentifier]fidl.Struct{},
		map[fidl.EncodedCompoundIdentifier]fidl.Struct{},
		map[fidl.EncodedCompoundIdentifier]Result{},
		map[string]HandleMetadataWrapper{},
	}

	for _, s := range r.Structs {
		c.structs[s.Name] = s
	}

	for _, v := range r.Bits {
		root.Bits = append(root.Bits, c.compileBits(v))
	}

	for _, v := range r.Consts {
		root.Consts = append(root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range r.Services {
		root.Services = append(root.Services, c.compileService(v))
	}

	for _, v := range r.Structs {
		if v.Anonymous {
			c.requestResponsePayload[v.Name] = v
		} else {
			root.Structs = append(root.Structs, c.compileStruct(v))
		}
	}

	for _, v := range r.Unions {
		if v.Attributes.HasAttribute("Result") {
			root.Results = append(root.Results, c.compileResultFromUnion(v, root))
		} else {
			root.Unions = append(root.Unions, c.compileUnion(v))
		}
	}

	for _, v := range r.Tables {
		root.Tables = append(root.Tables, c.compileTable(v))
	}

	for _, v := range r.Protocols {
		root.Protocols = append(root.Protocols, c.compileProtocol(v))
	}

	for _, hmw := range c.handleMetadataWrappers {
		root.HandleMetadataWrappers = append(root.HandleMetadataWrappers, hmw)
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
