// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type EncodedCompoundIdentifier = fidlgen.EncodedCompoundIdentifier

type Bits struct {
	fidlgen.Bits
	Name    string
	Type    string
	Members []BitsMember
}

type BitsMember struct {
	fidlgen.BitsMember
	Name  string
	Value string
}

type Const struct {
	fidlgen.Const
	Name  string
	Type  string
	Value string
}

type Enum struct {
	fidlgen.Enum
	Name    string
	Type    string
	Members []EnumMember
	// Member name with the minimum value, used as an arbitrary default value
	// in Decodable::new_empty for strict enums.
	MinMember string
}

type EnumMember struct {
	fidlgen.EnumMember
	Name  string
	Value string
}

type Union struct {
	fidlgen.Union
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []UnionMember
}

type UnionMember struct {
	fidlgen.UnionMember
	Type              string
	OGType            fidlgen.Type
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type ResultOkEntry struct {
	OGType            fidlgen.Type
	Type              string
	HasHandleMetadata bool
	HandleWrapperName string
}

type Result struct {
	fidlgen.Attributes
	ECI       EncodedCompoundIdentifier
	Derives   derives
	Name      string
	Ok        []ResultOkEntry
	ErrOGType fidlgen.Type
	ErrType   string
	Size      int
	Alignment int
}

type Struct struct {
	fidlgen.Struct
	ECI                                                  EncodedCompoundIdentifier
	Derives                                              derives
	Name                                                 string
	Members                                              []StructMember
	PaddingMarkersV1, PaddingMarkersV2                   []PaddingMarker
	FlattenedPaddingMarkersV1, FlattenedPaddingMarkersV2 []PaddingMarker
	SizeV1, SizeV2                                       int
	AlignmentV1, AlignmentV2                             int
	HasPadding                                           bool
	// True iff the fidl_struct_copy! macro should be used instead of fidl_struct!.
	UseFidlStructCopy bool
}

type StructMember struct {
	fidlgen.StructMember
	OGType             fidlgen.Type
	Type               string
	Name               string
	OffsetV1, OffsetV2 int
	HasDefault         bool
	DefaultValue       string
	HasHandleMetadata  bool
	HandleRights       string
	HandleSubtype      string
}

type PaddingMarker struct {
	Type   string
	Offset int
	// Mask is a string so it can be in hex.
	Mask string
}

type Table struct {
	fidlgen.Table
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []TableMember
}

type TableMember struct {
	fidlgen.TableMember
	OGType            fidlgen.Type
	Type              string
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type Protocol struct {
	fidlgen.Protocol
	ECI          EncodedCompoundIdentifier
	Name         string
	Methods      []Method
	ProtocolName string
}

type Method struct {
	fidlgen.Method
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
	OGType            fidlgen.Type
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
	fidlgen.Service
	Name        string
	Members     []ServiceMember
	ServiceName string
}

type ServiceMember struct {
	fidlgen.ServiceMember
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
	// TODO(fxbug.dev/66767): Remove "WaitForEvent".
	"wait_for_event": {},
	"WaitForEvent":   {},
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

func changeIfReserved(val fidlgen.Identifier) string {
	str := string(val)
	if hasReservedSuffix(str) || isReservedWord(str) {
		return str + "_"
	}
	return str
}

var primitiveTypes = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Bool:    "bool",
	fidlgen.Int8:    "i8",
	fidlgen.Int16:   "i16",
	fidlgen.Int32:   "i32",
	fidlgen.Int64:   "i64",
	fidlgen.Uint8:   "u8",
	fidlgen.Uint16:  "u16",
	fidlgen.Uint32:  "u32",
	fidlgen.Uint64:  "u64",
	fidlgen.Float32: "f32",
	fidlgen.Float64: "f64",
}

var handleSubtypes = map[fidlgen.HandleSubtype]string{
	fidlgen.Bti:          "Bti",
	fidlgen.Channel:      "Channel",
	fidlgen.Clock:        "Clock",
	fidlgen.DebugLog:     "DebugLog",
	fidlgen.Event:        "Event",
	fidlgen.Eventpair:    "EventPair",
	fidlgen.Exception:    "Exception",
	fidlgen.Fifo:         "Fifo",
	fidlgen.Guest:        "Guest",
	fidlgen.Handle:       "Handle",
	fidlgen.Interrupt:    "Interrupt",
	fidlgen.Iommu:        "Iommu",
	fidlgen.Job:          "Job",
	fidlgen.Pager:        "Pager",
	fidlgen.PciDevice:    "PciDevice",
	fidlgen.Pmt:          "Pmt",
	fidlgen.Port:         "Port",
	fidlgen.Process:      "Process",
	fidlgen.Profile:      "Profile",
	fidlgen.Resource:     "Resource",
	fidlgen.Socket:       "Socket",
	fidlgen.Stream:       "Stream",
	fidlgen.SuspendToken: "SuspendToken",
	fidlgen.Thread:       "Thread",
	fidlgen.Time:         "Timer",
	fidlgen.Vcpu:         "Vcpu",
	fidlgen.Vmar:         "Vmar",
	fidlgen.Vmo:          "Vmo",
}

var handleSubtypeConsts = map[fidlgen.HandleSubtype]string{
	fidlgen.Bti:          "BTI",
	fidlgen.Channel:      "CHANNEL",
	fidlgen.Clock:        "CLOCK",
	fidlgen.DebugLog:     "LOG",
	fidlgen.Event:        "EVENT",
	fidlgen.Eventpair:    "EVENTPAIR",
	fidlgen.Exception:    "EXCEPTION",
	fidlgen.Fifo:         "FIFO",
	fidlgen.Guest:        "GUEST",
	fidlgen.Handle:       "NONE",
	fidlgen.Interrupt:    "INTERRUPT",
	fidlgen.Iommu:        "IOMMU",
	fidlgen.Job:          "JOB",
	fidlgen.Pager:        "PAGER",
	fidlgen.PciDevice:    "PCI_DEVICE",
	fidlgen.Pmt:          "PMT",
	fidlgen.Port:         "PORT",
	fidlgen.Process:      "PROCESS",
	fidlgen.Profile:      "PROFILE",
	fidlgen.Resource:     "RESOURCE",
	fidlgen.Socket:       "SOCKET",
	fidlgen.Stream:       "STREAM",
	fidlgen.SuspendToken: "SUSPEND_TOKEN",
	fidlgen.Thread:       "THREAD",
	fidlgen.Time:         "TIMER",
	fidlgen.Vcpu:         "VCPU",
	fidlgen.Vmar:         "VMAR",
	fidlgen.Vmo:          "VMO",
}

type compiler struct {
	decls                  fidlgen.DeclInfoMap
	library                fidlgen.LibraryIdentifier
	externCrates           map[string]struct{}
	requestResponsePayload map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct
	structs                map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct
	results                map[fidlgen.EncodedCompoundIdentifier]Result
	handleMetadataWrappers map[string]HandleMetadataWrapper
}

func (c *compiler) inExternalLibrary(ci fidlgen.CompoundIdentifier) bool {
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

func compileCamelIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ToUpperCamelCase(changeIfReserved(val))
}

func compileLibraryName(library fidlgen.LibraryIdentifier) string {
	parts := []string{"fidl"}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(fidlgen.Identifier(strings.Join(parts, "_")))
}

func compileSnakeIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ToSnakeCase(changeIfReserved(val))
}

func compileScreamingSnakeIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ConstNameToAllCapsSnake(changeIfReserved(val))
}

func (c *compiler) compileCompoundIdentifier(val fidlgen.CompoundIdentifier) string {
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

func (c *compiler) compileCamelCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := fidlgen.ParseCompoundIdentifier(eci)
	val.Name = fidlgen.Identifier(compileCamelIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileSnakeCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := fidlgen.ParseCompoundIdentifier(eci)
	val.Name = fidlgen.Identifier(compileSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileScreamingSnakeCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := fidlgen.ParseCompoundIdentifier(eci)
	val.Name = fidlgen.Identifier(compileScreamingSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func compileLiteral(val fidlgen.Literal, typ fidlgen.Type) string {
	switch val.Kind {
	case fidlgen.StringLiteral:
		return fmt.Sprintf("r###\"%s\"###", val.Value)
	case fidlgen.NumericLiteral:
		if typ.Kind == fidlgen.PrimitiveType &&
			(typ.PrimitiveSubtype == fidlgen.Float32 || typ.PrimitiveSubtype == fidlgen.Float64) {
			if !strings.ContainsRune(val.Value, '.') {
				return fmt.Sprintf("%s.0", val.Value)
			}
			return val.Value
		}
		return val.Value
	case fidlgen.TrueLiteral:
		return "true"
	case fidlgen.FalseLiteral:
		return "false"
	case fidlgen.DefaultLiteral:
		return "::Default::default()"
	default:
		panic(fmt.Sprintf("unknown literal kind: %v", val.Kind))
	}
}

func (c *compiler) compileConstant(val fidlgen.Constant, typ fidlgen.Type) string {
	switch val.Kind {
	case fidlgen.IdentifierConstant:
		parts := fidlgen.ParseCompoundIdentifier(val.Identifier)
		if parts.Member == fidlgen.Identifier("") {
			// Top-level constant.
			parts.Name = fidlgen.Identifier(compileScreamingSnakeIdentifier(parts.Name))
		} else {
			// Bits or enum member.
			parts.Name = fidlgen.Identifier(compileCamelIdentifier(parts.Name))
			// TODO(fxbug.dev/47034) For bits the member should be SCREAMING_SNAKE_CASE.
			parts.Member = fidlgen.Identifier(compileCamelIdentifier(parts.Member))
		}
		return c.compileCompoundIdentifier(parts)
	case fidlgen.LiteralConstant:
		return compileLiteral(val.Literal, typ)
	case fidlgen.BinaryOperator:
		decl := c.compileType(typ)
		// from_bits isn't a const function, so from_bits_truncate must be used.
		return fmt.Sprintf("%s::from_bits_truncate(%s)", decl, val.Value)
	default:
		panic(fmt.Sprintf("unknown constant kind: %v", val.Kind))
	}
}

func (c *compiler) compileConst(val fidlgen.Const) Const {
	name := c.compileScreamingSnakeCompoundIdentifier(val.Name)
	var r Const
	if val.Type.Kind == fidlgen.StringType {
		r = Const{
			Const: val,
			Type:  "&str",
			Name:  name,
			Value: c.compileConstant(val.Value, val.Type),
		}
	} else {
		r = Const{
			Const: val,
			Type:  c.compileType(val.Type),
			Name:  name,
			Value: c.compileConstant(val.Value, val.Type),
		}
	}
	return r
}

func compilePrimitiveSubtype(val fidlgen.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

func compileHandleSubtype(val fidlgen.HandleSubtype) string {
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

func (c *compiler) fieldHandleInformation(val *fidlgen.Type) FieldHandleInformation {
	if val.ElementType != nil {
		return c.fieldHandleInformation(val.ElementType)
	}
	if val.Kind == fidlgen.RequestType {
		return FieldHandleInformation{
			fullObjectType:    "fidl::ObjectType::CHANNEL",
			fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
			shortObjectType:   "CHANNEL",
			shortRights:       "CHANNEL_DEFAULT",
			hasHandleMetadata: true,
		}
	}
	if val.Kind == fidlgen.IdentifierType {
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		if declInfo.Type == fidlgen.ProtocolDeclType {
			return FieldHandleInformation{
				fullObjectType:    "fidl::ObjectType::CHANNEL",
				fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
				shortObjectType:   "CHANNEL",
				shortRights:       "CHANNEL_DEFAULT",
				hasHandleMetadata: true,
			}
		}
	}
	if val.Kind != fidlgen.HandleType {
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

func (c *compiler) compileType(val fidlgen.Type) string {
	var t string
	switch val.Kind {
	case fidlgen.ArrayType:
		t = fmt.Sprintf("[%s; %v]", c.compileType(*val.ElementType), *val.ElementCount)
	case fidlgen.VectorType:
		t = fmt.Sprintf("Vec<%s>", c.compileType(*val.ElementType))
	case fidlgen.StringType:
		t = "String"
	case fidlgen.HandleType:
		t = fmt.Sprintf("fidl::%s", compileHandleSubtype(val.HandleSubtype))
	case fidlgen.RequestType:
		t = fmt.Sprintf("fidl::endpoints::ServerEnd<%sMarker>", c.compileCamelCompoundIdentifier(val.RequestSubtype))
	case fidlgen.PrimitiveType:
		t = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case fidlgen.IdentifierType:
		t = c.compileCamelCompoundIdentifier(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.UnionDeclType:
			if val.Nullable {
				t = fmt.Sprintf("Box<%s>", t)
			}
		case fidlgen.ProtocolDeclType:
			t = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}

	if val.Nullable {
		return fmt.Sprintf("Option<%s>", t)
	}
	return t
}

func (c *compiler) compileBorrowedType(val fidlgen.Type) string {
	var t string
	switch val.Kind {
	case fidlgen.PrimitiveType, fidlgen.HandleType, fidlgen.RequestType:
		return c.compileType(val)
	case fidlgen.ArrayType:
		t = fmt.Sprintf("&mut [%s; %v]", c.compileBorrowedType(*val.ElementType), *val.ElementCount)
	case fidlgen.VectorType:
		t = c.compileBorrowedType(*val.ElementType)
		// We use slices for primitive numeric types so that encoding becomes a
		// memcpy. Rust does not guarantee the bit patterns for bool values, so
		// we omit them from the optimization.
		if val.ElementType.Kind == fidlgen.PrimitiveType && val.ElementType.PrimitiveSubtype != fidlgen.Bool {
			t = fmt.Sprintf("&[%s]", t)
		} else {
			t = fmt.Sprintf("&mut dyn ExactSizeIterator<Item = %s>", t)
		}
	case fidlgen.StringType:
		t = "&str"
	case fidlgen.IdentifierType:
		t = c.compileCamelCompoundIdentifier(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.UnionDeclType:
			t = fmt.Sprintf("&mut %s", t)
		case fidlgen.ProtocolDeclType:
			t = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}

	if val.Nullable {
		return fmt.Sprintf("Option<%s>", t)
	}
	return t
}

func (c *compiler) compileBits(val fidlgen.Bits) Bits {
	e := Bits{
		Bits:    val,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Type:    c.compileType(val.Type),
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

func (c *compiler) compileEnum(val fidlgen.Enum) Enum {
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
			Value: c.compileConstant(v.Value, fidlgen.Type{
				Kind:             fidlgen.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
		})
	}
	e.MinMember = findMinEnumMember(val.Type, e.Members).Name
	return e
}

func findMinEnumMember(typ fidlgen.PrimitiveSubtype, members []EnumMember) EnumMember {
	var res EnumMember
	if typ.IsSigned() {
		min := int64(math.MaxInt64)
		for _, m := range members {
			v, err := strconv.ParseInt(m.Value, 10, 64)
			if err != nil {
				panic(fmt.Sprintf("invalid enum member value: %s", err))
			}
			if v < min {
				min = v
				res = m
			}
		}
	} else {
		min := uint64(math.MaxUint64)
		for _, m := range members {
			v, err := strconv.ParseUint(m.Value, 10, 64)
			if err != nil {
				panic(fmt.Sprintf("invalid enum member value: %s", err))
			}
			if v < min {
				min = v
				res = m
			}
		}
	}
	return res
}

func (c *compiler) compileHandleMetadataWrapper(val *fidlgen.Type) (string, bool) {
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

func (c *compiler) compileParameterArray(payload fidlgen.EncodedCompoundIdentifier) []Parameter {
	val, ok := c.requestResponsePayload[payload]
	if !ok {
		panic(fmt.Sprintf("unknown request/response struct: %v", payload))
	}

	var parameters []Parameter
	for _, v := range val.Members {
		wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&v.Type)
		parameters = append(parameters, Parameter{
			OGType:            v.Type,
			Type:              c.compileType(v.Type),
			BorrowedType:      c.compileBorrowedType(v.Type),
			Name:              compileSnakeIdentifier(v.Name),
			HandleWrapperName: wrapperName,
			HasHandleMetadata: hasHandleMetadata,
		})
	}
	return parameters
}

// TODO(fxbug.dev/76655): Remove this.
const maximumAllowedParameters = 12

func (c *compiler) compileProtocol(val fidlgen.Protocol) Protocol {
	r := Protocol{
		Protocol:     val,
		ECI:          val.Name,
		Name:         c.compileCamelCompoundIdentifier(val.Name),
		Methods:      []Method{},
		ProtocolName: strings.Trim(val.GetServiceName(), "\""),
	}

	for _, v := range val.Methods {
		if len(v.Request) > maximumAllowedParameters {
			panic(fmt.Sprintf(
				`Method %s.%s has %d parameters, but the FIDL Rust bindings `+
					`only support up to %d. See https://fxbug.dev/76655 for details.`,
				val.Name, v.Name, len(v.Request), maximumAllowedParameters))
		}

		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		var foundResult *Result
		if len(v.Response) == 1 && v.Response[0].Type.Kind == fidlgen.IdentifierType {
			responseType := v.Response[0].Type
			if result, ok := c.results[responseType.Identifier]; ok {
				foundResult = &result
			}
		}
		m := Method{
			Method:         v,
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

func (c *compiler) compileService(val fidlgen.Service) Service {
	r := Service{
		Service:     val,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Members:     []ServiceMember{},
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		m := ServiceMember{
			ServiceMember: v,
			Name:          string(v.Name),
			CamelName:     compileCamelIdentifier(v.Name),
			SnakeName:     compileSnakeIdentifier(v.Name),
			ProtocolType:  c.compileCamelCompoundIdentifier(v.Type.Identifier),
		}
		r.Members = append(r.Members, m)
	}

	return r
}

func (c *compiler) compileStructMember(val fidlgen.StructMember) StructMember {
	hi := c.fieldHandleInformation(&val.Type)
	return StructMember{
		StructMember:      val,
		Type:              c.compileType(val.Type),
		OGType:            val.Type,
		Name:              compileSnakeIdentifier(val.Name),
		OffsetV1:          val.FieldShapeV1.Offset,
		OffsetV2:          val.FieldShapeV2.Offset,
		HasDefault:        false,
		DefaultValue:      "", // TODO(cramertj) support defaults
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func getTypeShapeV1(s fidlgen.Struct) fidlgen.TypeShape {
	return s.TypeShapeV1
}
func getTypeShapeV2(s fidlgen.Struct) fidlgen.TypeShape {
	return s.TypeShapeV2
}
func getFieldShapeV1(m fidlgen.StructMember) fidlgen.FieldShape {
	return m.FieldShapeV1
}
func getFieldShapeV2(m fidlgen.StructMember) fidlgen.FieldShape {
	return m.FieldShapeV2
}

func (c *compiler) populateFullStructMaskForStruct(mask []byte, val fidlgen.Struct, flatten bool, getTypeShape func(fidlgen.Struct) fidlgen.TypeShape, getFieldShape func(fidlgen.StructMember) fidlgen.FieldShape) {
	paddingEnd := getTypeShape(val).InlineSize - 1
	for i := len(val.Members) - 1; i >= 0; i-- {
		member := val.Members[i]
		fieldShape := getFieldShape(member)
		if flatten {
			c.populateFullStructMaskForType(mask[fieldShape.Offset:paddingEnd+1], &member.Type, flatten, getTypeShape, getFieldShape)
		}
		for j := 0; j < fieldShape.Padding; j++ {
			mask[paddingEnd-j] = 0xff
		}
		paddingEnd = fieldShape.Offset - 1
	}
}

func (c *compiler) populateFullStructMaskForType(mask []byte, typ *fidlgen.Type, flatten bool, getTypeShape func(fidlgen.Struct) fidlgen.TypeShape, getFieldShape func(fidlgen.StructMember) fidlgen.FieldShape) {
	if typ.Nullable {
		return
	}
	switch typ.Kind {
	case fidlgen.ArrayType:
		elemByteSize := len(mask) / *typ.ElementCount
		for i := 0; i < *typ.ElementCount; i++ {
			c.populateFullStructMaskForType(mask[i*elemByteSize:(i+1)*elemByteSize], typ.ElementType, flatten, getTypeShape, getFieldShape)
		}
	case fidlgen.IdentifierType:
		if c.inExternalLibrary(fidlgen.ParseCompoundIdentifier(typ.Identifier)) {
			// This behavior is matched by computeUseFullStructCopy.
			return
		}
		declType := c.decls[typ.Identifier].Type
		if declType == fidlgen.StructDeclType {
			st, ok := c.structs[typ.Identifier]
			if !ok {
				panic(fmt.Sprintf("struct not found: %v", typ.Identifier))
			}
			c.populateFullStructMaskForStruct(mask, st, flatten, getTypeShape, getFieldShape)
		}
	}
}

func (c *compiler) buildPaddingMarkers(val fidlgen.Struct, flatten bool, getTypeShape func(fidlgen.Struct) fidlgen.TypeShape, getFieldShape func(fidlgen.StructMember) fidlgen.FieldShape) []PaddingMarker {
	var paddingMarkers []PaddingMarker

	// Construct a mask across the whole struct with 0xff bytes where there is padding.
	fullStructMask := make([]byte, getTypeShape(val).InlineSize)
	c.populateFullStructMaskForStruct(fullStructMask, val, flatten, getTypeShape, getFieldShape)

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

func (c *compiler) computeUseFidlStructCopyForStruct(st fidlgen.Struct) bool {
	for _, member := range st.Members {
		if !c.computeUseFidlStructCopy(&member.Type) {
			return false
		}
	}
	return true
}

func (c *compiler) computeUseFidlStructCopy(typ *fidlgen.Type) bool {
	if typ.Nullable {
		return false
	}
	switch typ.Kind {
	case fidlgen.ArrayType:
		return c.computeUseFidlStructCopy(typ.ElementType)
	case fidlgen.VectorType, fidlgen.StringType, fidlgen.HandleType, fidlgen.RequestType:
		return false
	case fidlgen.PrimitiveType:
		switch typ.PrimitiveSubtype {
		case fidlgen.Bool, fidlgen.Float32, fidlgen.Float64:
			return false
		}
		return true
	case fidlgen.IdentifierType:
		if c.inExternalLibrary(fidlgen.ParseCompoundIdentifier(typ.Identifier)) {
			return false
		}
		declType := c.decls[typ.Identifier].Type
		switch declType {
		case fidlgen.BitsDeclType, fidlgen.EnumDeclType, fidlgen.TableDeclType, fidlgen.UnionDeclType, fidlgen.ProtocolDeclType:
			return false
		case fidlgen.StructDeclType:
			st, ok := c.structs[typ.Identifier]
			if !ok {
				panic(fmt.Sprintf("struct not found: %v", typ.Identifier))
			}
			return c.computeUseFidlStructCopyForStruct(st)
		default:
			panic(fmt.Sprintf("unknown declaration type: %v", declType))
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", typ.Kind))
	}
}

func (c *compiler) compileStruct(val fidlgen.Struct) Struct {
	name := c.compileCamelCompoundIdentifier(val.Name)
	r := Struct{
		Struct:                    val,
		ECI:                       val.Name,
		Name:                      name,
		Members:                   []StructMember{},
		SizeV1:                    val.TypeShapeV1.InlineSize,
		SizeV2:                    val.TypeShapeV2.InlineSize,
		AlignmentV1:               val.TypeShapeV1.Alignment,
		AlignmentV2:               val.TypeShapeV2.Alignment,
		PaddingMarkersV1:          c.buildPaddingMarkers(val, false, getTypeShapeV1, getFieldShapeV1),
		PaddingMarkersV2:          c.buildPaddingMarkers(val, false, getTypeShapeV2, getFieldShapeV2),
		FlattenedPaddingMarkersV1: c.buildPaddingMarkers(val, true, getTypeShapeV1, getFieldShapeV1),
		FlattenedPaddingMarkersV2: c.buildPaddingMarkers(val, true, getTypeShapeV2, getFieldShapeV2),
	}

	for _, v := range val.Members {
		member := c.compileStructMember(v)
		r.Members = append(r.Members, member)
		r.HasPadding = r.HasPadding || (v.FieldShapeV1.Padding != 0)
	}

	r.UseFidlStructCopy = c.computeUseFidlStructCopyForStruct(val)

	return r
}

func (c *compiler) compileUnionMember(val fidlgen.UnionMember) UnionMember {
	hi := c.fieldHandleInformation(&val.Type)
	return UnionMember{
		UnionMember:       val,
		Type:              c.compileType(val.Type),
		OGType:            val.Type,
		Name:              compileCamelIdentifier(val.Name),
		Ordinal:           val.Ordinal,
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func (c *compiler) compileUnion(val fidlgen.Union) Union {
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

func (c *compiler) compileResultFromUnion(val fidlgen.Union, mr fidlgen.MethodResult, root Root) Result {
	r := Result{
		Attributes: val.Attributes,
		ECI:        val.Name,
		Name:       c.compileCamelCompoundIdentifier(val.Name),
		Ok:         []ResultOkEntry{},
		ErrOGType:  mr.ErrorType,
		ErrType:    c.compileUnionMember(*mr.ErrorMember).Type,
		Size:       val.TypeShapeV1.InlineSize,
		Alignment:  val.TypeShapeV1.Alignment,
	}

	for _, m := range root.findStruct(mr.ValueStruct.Name).Members {
		wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&m.OGType)
		r.Ok = append(r.Ok, ResultOkEntry{
			OGType:            m.OGType,
			Type:              m.Type,
			HasHandleMetadata: hasHandleMetadata,
			HandleWrapperName: wrapperName,
		})
	}

	c.results[r.ECI] = r

	return r
}

func (c *compiler) compileTable(table fidlgen.Table) Table {
	var members []TableMember
	for _, member := range table.SortedMembersNoReserved() {
		hi := c.fieldHandleInformation(&member.Type)
		members = append(members, TableMember{
			TableMember:       member,
			OGType:            member.Type,
			Type:              c.compileType(member.Type),
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
	declInfo, ok := dc.decls[eci]
	if !ok {
		panic(fmt.Sprintf("declaration not found: %v", eci))
	}

	// TODO(fxbug.dev/61760): Make external type information available here.
	// Currently, we conservatively assume external structs/tables/unions only
	// derive the minimal set of traits, plus Clone for value types (not having
	// Clone is especially annoying, so we put resourceness of external types
	// into the IR as a stopgap solution).
	if dc.inExternalLibrary(fidlgen.ParseCompoundIdentifier(eci)) {
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.TableDeclType, fidlgen.UnionDeclType:
			if declInfo.IsValueType() {
				return derivesMinimalNonResource
			}
			return derivesMinimal
		}
	}

	// Return early for declaration types that do not require recursion.
	switch declInfo.Type {
	case fidlgen.ConstDeclType:
		panic("const decl should never have derives")
	case fidlgen.BitsDeclType, fidlgen.EnumDeclType:
		// Enums and bits are always simple, non-float primitives which
		// implement all derivable traits except zerocopy.
		return derivesAllButZerocopy
	case fidlgen.ProtocolDeclType:
		// When a protocol is used as a type, it means a ClientEnd in Rust.
		return derivesAllButZerocopy.remove(derivesCopy, derivesClone)
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

	var derivesOut derives
typeSwitch:
	switch declInfo.Type {
	case fidlgen.StructDeclType:
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
	case fidlgen.TableDeclType:
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
		table.Derives = derivesOut
	case fidlgen.UnionDeclType:
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
			if union.IsFlexible() {
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
		panic(fmt.Sprintf("unknown declaration type: %v", declInfo.Type))
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

func (dc *derivesCompiler) fillDerivesForType(ogType fidlgen.Type) derives {
	switch ogType.Kind {
	case fidlgen.ArrayType:
		return dc.fillDerivesForType(*ogType.ElementType)
	case fidlgen.VectorType:
		return derivesAllButZerocopy.remove(derivesCopy).and(dc.fillDerivesForType(*ogType.ElementType))
	case fidlgen.StringType:
		return derivesAllButZerocopy.remove(derivesCopy)
	case fidlgen.HandleType, fidlgen.RequestType:
		return derivesAllButZerocopy.remove(derivesCopy, derivesClone)
	case fidlgen.PrimitiveType:
		switch ogType.PrimitiveSubtype {
		case fidlgen.Bool:
			return derivesAllButZerocopy
		case fidlgen.Int8, fidlgen.Int16, fidlgen.Int32, fidlgen.Int64:
			return derivesAll
		case fidlgen.Uint8, fidlgen.Uint16, fidlgen.Uint32, fidlgen.Uint64:
			return derivesAll
		case fidlgen.Float32, fidlgen.Float64:
			// Floats don't have a total ordering due to NAN and its multiple representations.
			return derivesAllButZerocopy.remove(derivesEq, derivesOrd, derivesHash)
		default:
			panic(fmt.Sprintf("unknown primitive type: %v", ogType.PrimitiveSubtype))
		}
	case fidlgen.IdentifierType:
		internalTypeDerives := dc.fillDerivesForECI(ogType.Identifier)
		if ogType.Nullable {
			// A nullable struct/union gets put in Option<Box<...>> and so
			// cannot derive Copy, AsBytes, or FromBytes. Bits, enums, and
			// tables are never nullable. The only other possibility for an
			// identifier type is a protocol, which cannot derive these either.
			return internalTypeDerives.remove(derivesCopy, derivesAsBytes, derivesFromBytes)
		}
		return internalTypeDerives
	default:
		panic(fmt.Sprintf("unknown type kind: %v", ogType.Kind))
	}
}

func Compile(r fidlgen.Root) Root {
	r = r.ForBindings("rust")
	root := Root{}
	thisLibParsed := fidlgen.ParseLibraryName(r.Name)
	c := compiler{
		r.DeclsWithDependencies(),
		thisLibParsed,
		map[string]struct{}{},
		map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{},
		map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{},
		map[fidlgen.EncodedCompoundIdentifier]Result{},
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
		if v.IsRequestOrResponse {
			c.requestResponsePayload[v.Name] = v
		} else {
			root.Structs = append(root.Structs, c.compileStruct(v))
		}
	}

	for _, v := range r.Unions {
		if v.MethodResult != nil {
			root.Results = append(root.Results,
				c.compileResultFromUnion(v, *v.MethodResult, root))
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

	// Sort by wrapper name for deterministic output order.
	handleMetadataWrapperNames := make([]string, 0, len(c.handleMetadataWrappers))
	for name := range c.handleMetadataWrappers {
		handleMetadataWrapperNames = append(handleMetadataWrapperNames, name)
	}
	sort.Strings(handleMetadataWrapperNames)
	for _, name := range handleMetadataWrapperNames {
		root.HandleMetadataWrappers = append(root.HandleMetadataWrappers, c.handleMetadataWrappers[name])
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
