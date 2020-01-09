// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"math"
	"sort"
	"strconv"
	"strings"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
)

const (
	ProxySuffix            = "Interface"
	StubSuffix             = "Stub"
	EventProxySuffix       = "EventProxy"
	ServiceSuffix          = "Service"
	TransitionalBaseSuffix = "TransitionalBase"
	ServiceNameSuffix      = "Name"
	RequestSuffix          = "InterfaceRequest"
	TagSuffix              = "Tag"
	WithCtxSuffix          = "WithCtx"

	MessageHeaderSize = 16

	SyscallZxPackage = "syscall/zx"
	SyscallZxAlias   = "_zx"

	BindingsPackage = "syscall/zx/fidl"
	BindingsAlias   = "_bindings"
)

// Type represents a golang type.
type Type string

// Const represents the idiomatic representation of a constant in golang.
type Const struct {
	types.Attributes

	// Name is the name of the constant.
	Name string

	// Type is the constant's type.
	Type Type

	// Value is the constant's value.
	Value string
}

// Bits represents the idiomatic representation of an bits in golang.
//
// That is, something like:
// type MyBits int32
// const (
//    MyBitsMember1 MyBits = 1
//    MyBitsMember2        = 4
//    ...
// )
type Bits struct {
	types.Attributes

	// Name is the name of the bits type alias.
	Name string

	// Type is the underlying primitive type for the bits.
	Type Type

	// Members is the list of bits variants that are a part of this bits.
	// The values of the Members must not overlap.
	Members []BitsMember
}

// BitsMember represents a single bits variant. See Bits for more details.
type BitsMember struct {
	types.Attributes

	// Name is the name of the bits variant without any prefix.
	Name string

	// Value is the raw value of the bits variant, represented as a string
	// to support many types.
	Value string
}

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
	types.Attributes

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
	types.Attributes

	// Name is the name of the enum variant without any prefix.
	Name string

	// Value is the raw value of the enum variant, represented as a string
	// to support many types.
	Value string
}

// Struct represents a golang struct.
type Struct struct {
	types.Attributes

	// Name is the name of the golang struct.
	Name string

	// Members is a list of the golang struct members.
	Members []StructMember

	// InlineSizeOld is the FIDL-encoded size of the struct.
	InlineSizeOld int
	// AlignmentOld is the alignment of the FIDL-encoded struct.
	AlignmentOld int

	// InlineSizeV1 is the FIDL-encoded size of the struct.
	InlineSizeV1 int
	// AlignmentV1 is the alignment of the FIDL-encoded struct.
	AlignmentV1 int
}

type Tag struct {
	reverseOfBounds []int
}

// String generates a string representation for the tag.
func (t Tag) String() string {
	var (
		elems    []string
		allEmpty = true
	)
	for i := len(t.reverseOfBounds) - 1; 0 <= i; i-- {
		bound := t.reverseOfBounds[i]
		if bound == math.MaxInt32 {
			elems = append(elems, "")
		} else {
			elems = append(elems, strconv.Itoa(bound))
			allEmpty = false
		}
	}
	if allEmpty {
		return ""
	}
	return fmt.Sprintf(`%s`, strings.Join(elems, ","))
}

// StructMember represents the member of a golang struct.
type StructMember struct {
	types.Attributes

	// Name is the name of the golang struct member.
	Name string

	// PrivateName is the unexported version of the name of the struct member.
	PrivateName string

	// Type is the type of the golang struct member.
	Type Type

	// Corresponds to fidl tag in generated go.
	FidlTag string

	// Field offset for the V1 wire format, without efficient envelopes.
	OffsetV1 int
}

type XUnion struct {
	types.Attributes
	Name          string
	TagName       string
	Members       []XUnionMember
	InlineSizeOld int
	AlignmentOld  int
	InlineSizeV1  int
	AlignmentV1   int
	types.Strictness
}

type XUnionMember struct {
	types.Attributes
	Ordinal         uint64
	ExplicitOrdinal uint64
	HashedOrdinal   uint64
	Name            string
	PrivateName     string
	Type            Type
	FidlTag         string
}

// Table represents a FIDL table as a golang struct.
type Table struct {
	types.Attributes
	Name          string
	Members       []TableMember
	InlineSizeOld int
	AlignmentOld  int
	InlineSizeV1  int
	AlignmentV1   int
}

// TableMember represents a FIDL table member as two golang struct members, one
// for the member itself, and one to indicate presence or absence.
type TableMember struct {
	types.Attributes

	// DataField is the exported name of the FIDL table member.
	DataField string

	// PrivateDataField is an unexported name of the FIDL table member, used as
	// argument.
	PrivateDataField string

	// PresenceField is the exported name of boolean indicating presence of
	// the FIDL table member.
	PresenceField string

	// Setter is the exported name of the FIDL table member setter.
	Setter string

	// Getter is the exported name of the FIDL table member getter.
	Getter string

	// GetterWithDefault is the exported name of the FIDL table member getter
	// with a default value.
	GetterWithDefault string

	// Clearer is the exported name of the FIDL table member clearer.
	Clearer string

	// Haser is the exported name of the presence checker of the FIDL table
	// member.
	Haser string

	// Type is the golang type of the table member.
	Type Type

	// Corresponds to fidl: tag in generated go.
	FidlTag string
}

// Interface represents a FIDL interface in terms of golang structures.
type Interface struct {
	types.Attributes

	// Name is the Golang name of the interface.
	Name string

	// WithCtxName is the Golang name of the interface where every method takes a Context
	// as their first parameter.
	WithCtxName string

	// ProxyName is the name of the proxy type for this FIDL interface.
	ProxyName string

	// ProxyType is concrete type of proxy used for this FIDL interface.
	ProxyType string

	// StubName is the name of the stub type for this FIDL interface.
	StubName string

	// EventProxyName is the name of the event proxy type for this FIDL interface.
	EventProxyName string

	// TransitionalBaseName is the name of the base implementation for transitional methods
	// for this FIDL interface.
	TransitionalBaseName string

	// TransitionalBaseWithCtxName is the name of the base implementation
	// for transitional methods for this FIDL interface with a Context parameter.
	TransitionalBaseWithCtxName string

	// RequestName is the name of the interface request type for this FIDL interface.
	RequestName string

	// ServerName is the name of the server type for this FIDL interface.
	ServerName string

	// ServiceNameString is the string service name for this FIDL interface.
	ServiceNameString string

	// ServiceNameConstant is the name of the service name constant for this FIDL interface.
	ServiceNameConstant string

	// Methods is a list of methods for this FIDL interface.
	Methods []Method
}

// Method represents a method of a FIDL interface in terms of golang structures.
type Method struct {
	types.Attributes
	types.Ordinals

	// Name is the name of the Method, including the interface name as a prefix.
	Name string

	// Request represents a goland struct containing the request parameters.
	Request *Struct

	// Response represents an optional golang struct containing the response parameters.
	Response *Struct

	// EventExpectName is the name of the method for the client-side event proxy.
	// Only relevant if the method is an event.
	EventExpectName string

	// IsEvent is set to true if the method is an event. In this case, Response will always be
	// non-nil while Request will always be nil. EventExpectName will also be non-empty.
	IsEvent bool

	// IsTransitional is set to true if the method has the Transitional attribute.
	IsTransitional bool
}

// Library represents a FIDL library as a golang package.
type Library struct {
	// Alias is the alias of the golang package referring to a FIDL library.
	Alias string

	// Path is the path to the golang package referring to a FIDL library.
	Path string
}

// Root is the root of the golang backend IR structure.
//
// The golang backend IR structure is loosely modeled after an abstract syntax
// tree, and is used to generate golang code from templates.
type Root struct {
	// Name is the name of the library.
	Name string

	// PackageName is the name of the golang package as other Go programs would
	// import it.
	PackageName string

	// Bits represents a list of FIDL bits represented as Go bits.
	Bits []Bits

	// Consts represents a list of FIDL constants represented as Go constants.
	Consts []Const

	// Enums represents a list of FIDL enums represented as Go enums.
	Enums []Enum

	// Structs represents the list of FIDL structs represented as Go structs.
	Structs []Struct

	// XUnions represents the list of FIDL xunions represented as Go structs.
	XUnions []XUnion

	// Table represents the list of FIDL tables represented as Go structs.
	Tables []Table

	// Interfaces represents the list of FIDL interfaces represented as Go types.
	Interfaces []Interface

	// Libraries represents the set of library dependencies for this FIDL library.
	Libraries []Library
}

// compiler contains the state necessary for recursive compilation.
type compiler struct {
	// decls contains all top-level declarations for the FIDL source.
	decls types.DeclMap

	// library is the identifier for the current library.
	library types.LibraryIdentifier

	// libraryDeps is a mapping of compiled library identifiers (go package paths)
	// to aliases, which is used to resolve references to types outside of the current
	// FIDL library.
	libraryDeps map[string]string

	// usedLibraryDeps is identical to libraryDeps except it is built up as references
	// are made into libraryDeps. Thus, after Compile is run, it contains the subset of
	// libraryDeps that's actually being used. The purpose is to figure out which
	// dependencies need to be imported.
	usedLibraryDeps map[string]string
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

var handleTypes = map[types.HandleSubtype]string{
	// TODO(mknyszek): Add support here for process, thread, job, resource,
	// interrupt, eventpair, fifo, guest, and time once these are actually
	// supported in the Go runtime.
	types.Handle:   "_zx.Handle",
	types.Vmo:      "_zx.VMO",
	types.Channel:  "_zx.Channel",
	types.Event:    "_zx.Event",
	types.Port:     "_zx.Port",
	types.DebugLog: "_zx.Log",
	types.Socket:   "_zx.Socket",
	types.Vmar:     "_zx.VMAR",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(val types.Identifier, ext string) string {
	// TODO(mknyszek): Detect name collision within a scope as a result of transforming.
	str := string(val) + ext
	if isReservedWord(str) {
		return str + "_"
	}
	return str
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

func (_ *compiler) compileIdentifier(id types.Identifier, export bool, ext string) string {
	str := string(id)
	if export {
		str = common.ToUpperCamelCase(str)
	} else {
		str = common.ToLowerCamelCase(str)
	}
	return changeIfReserved(types.Identifier(str), ext)
}

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, export bool, ext string) string {
	ci := types.ParseCompoundIdentifier(eci)
	var name string
	if export {
		name = common.ToUpperCamelCase(string(ci.Name))
	} else {
		name = common.ToLowerCamelCase(string(ci.Name))
	}
	pkg := compileLibraryIdentifier(ci.Library)
	var strs []string
	if c.inExternalLibrary(ci) {
		pkgAlias := c.libraryDeps[pkg]
		strs = append(strs, pkgAlias)
		c.usedLibraryDeps[pkg] = pkgAlias
	}
	strs = append(strs, changeIfReserved(types.Identifier(name), ext))
	if ci.Member != "" {
		strs = append(strs, string(ci.Member))
	}
	return strings.Join(strs, ".")
}

func (_ *compiler) compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.NumericLiteral:
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.StringLiteral:
		return strconv.Quote(val.Value)
	default:
		log.Fatal("Unknown literal kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileCompoundIdentifier(val.Identifier, true, "")
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) Type {
	t, ok := primitiveTypes[val]
	if !ok {
		log.Fatal("Unknown primitive type: ", val)
	}
	return Type(t)
}

func (c *compiler) compileType(val types.Type) (r Type, t Tag) {
	switch val.Kind {
	case types.ArrayType:
		e, et := c.compileType(*val.ElementType)
		r = Type(fmt.Sprintf("[%s]%s", strconv.Itoa(*val.ElementCount), e))
		t = et
	case types.StringType:
		if val.ElementCount == nil {
			t.reverseOfBounds = append(t.reverseOfBounds, math.MaxInt32)
		} else {
			t.reverseOfBounds = append(t.reverseOfBounds, *val.ElementCount)
		}
		if val.Nullable {
			r = Type("*string")
		} else {
			r = Type("string")
		}
	case types.HandleType:
		// Note here that we require the SyscallZx package.
		c.usedLibraryDeps[SyscallZxPackage] = SyscallZxAlias
		e, ok := handleTypes[val.HandleSubtype]
		if !ok {
			// Fall back onto a generic handle if we don't support that particular
			// handle subtype.
			e = handleTypes[types.Handle]
		}
		var nullability int
		if val.Nullable {
			nullability = 1
		}
		t.reverseOfBounds = append(t.reverseOfBounds, nullability)
		r = Type(e)
	case types.RequestType:
		e := c.compileCompoundIdentifier(val.RequestSubtype, true, RequestSuffix)
		var nullability int
		if val.Nullable {
			nullability = 1
		}
		t.reverseOfBounds = append(t.reverseOfBounds, nullability)
		r = Type(e)
	case types.VectorType:
		e, et := c.compileType(*val.ElementType)
		if val.ElementCount == nil {
			et.reverseOfBounds = append(et.reverseOfBounds, math.MaxInt32)
		} else {
			et.reverseOfBounds = append(et.reverseOfBounds, *val.ElementCount)
		}
		if val.Nullable {
			r = Type(fmt.Sprintf("*[]%s", e))
		} else {
			r = Type(fmt.Sprintf("[]%s", e))
		}
		t = et
	case types.PrimitiveType:
		r = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		e := c.compileCompoundIdentifier(val.Identifier, true, "")
		declType, ok := c.decls[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.BitsDeclType:
			fallthrough
		case types.EnumDeclType:
			r = Type(e)
		case types.InterfaceDeclType:
			r = Type(e + ProxySuffix)
		case types.StructDeclType:
			fallthrough
		case types.UnionDeclType:
			fallthrough
		case types.XUnionDeclType:
			fallthrough
		case types.TableDeclType:
			if val.Nullable {
				r = Type("*" + e)
			} else {
				r = Type(e)
			}
		default:
			log.Fatal("Unknown declaration type: ", declType)
		}
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}
	return
}

func (c *compiler) compileBitsMember(val types.BitsMember) BitsMember {
	return BitsMember{
		Attributes: val.Attributes,
		Name:       c.compileIdentifier(val.Name, true, ""),
		Value:      c.compileConstant(val.Value),
	}
}

func (c *compiler) compileBits(val types.Bits) Bits {
	t, _ := c.compileType(val.Type)
	r := Bits{
		Attributes: val.Attributes,
		Name:       c.compileCompoundIdentifier(val.Name, true, ""),
		Type:       t,
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileBitsMember(v))
	}
	return r
}

func (c *compiler) compileConst(val types.Const) Const {
	// It's OK to ignore the tag because this type is guaranteed by the frontend
	// to be either an enum, a primitive, or a string.
	t, _ := c.compileType(val.Type)
	return Const{
		Attributes: val.Attributes,
		Name:       c.compileCompoundIdentifier(val.Name, true, ""),
		Type:       t,
		Value:      c.compileConstant(val.Value),
	}
}

func (c *compiler) compileEnumMember(val types.EnumMember) EnumMember {
	return EnumMember{
		Attributes: val.Attributes,
		Name:       c.compileIdentifier(val.Name, true, ""),
		Value:      c.compileConstant(val.Value),
	}
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	r := Enum{
		Attributes: val.Attributes,
		Name:       c.compileCompoundIdentifier(val.Name, true, ""),
		Type:       c.compilePrimitiveSubtype(val.Type),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileEnumMember(v))
	}
	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	ty, tag := c.compileType(val.Type)
	tag.reverseOfBounds = append(tag.reverseOfBounds, val.FieldShapeOld.Offset)
	return StructMember{
		Attributes:  val.Attributes,
		Type:        ty,
		Name:        c.compileIdentifier(val.Name, true, ""),
		PrivateName: c.compileIdentifier(val.Name, false, ""),
		FidlTag:     tag.String(),
		OffsetV1:    val.FieldShapeV1NoEE.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	r := Struct{
		Attributes:    val.Attributes,
		Name:          c.compileCompoundIdentifier(val.Name, true, ""),
		InlineSizeOld: val.TypeShapeOld.InlineSize,
		AlignmentOld:  val.TypeShapeOld.Alignment,
		InlineSizeV1:  val.TypeShapeV1NoEE.InlineSize,
		AlignmentV1:   val.TypeShapeV1NoEE.Alignment,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	return r
}

func (c *compiler) compileXUnion(val types.XUnion) XUnion {
	var members []XUnionMember
	for _, member := range val.Members {
		if member.Reserved {
			continue
		}
		ty, tag := c.compileType(member.Type)
		tag.reverseOfBounds = append(tag.reverseOfBounds, member.Ordinal)
		members = append(members, XUnionMember{
			Attributes:      member.Attributes,
			Ordinal:         uint64(member.Ordinal),
			ExplicitOrdinal: uint64(member.ExplicitOrdinal),
			HashedOrdinal:   uint64(member.HashedOrdinal),
			Type:            ty,
			Name:            c.compileIdentifier(member.Name, true, ""),
			PrivateName:     c.compileIdentifier(member.Name, false, ""),
			FidlTag:         tag.String(),
		})
	}
	return XUnion{
		Attributes:    val.Attributes,
		Name:          c.compileCompoundIdentifier(val.Name, true, ""),
		TagName:       "I_" + c.compileCompoundIdentifier(val.Name, false, TagSuffix),
		InlineSizeOld: val.TypeShapeOld.InlineSize,
		AlignmentOld:  val.TypeShapeOld.Alignment,
		InlineSizeV1:  val.TypeShapeV1NoEE.InlineSize,
		AlignmentV1:   val.TypeShapeV1NoEE.Alignment,
		Members:       members,
		Strictness:    val.Strictness,
	}
}

func (c *compiler) compileTable(val types.Table) Table {
	var members []TableMember
	for _, member := range val.SortedMembersNoReserved() {
		var (
			ty, tag     = c.compileType(member.Type)
			name        = c.compileIdentifier(member.Name, true, "")
			privateName = c.compileIdentifier(member.Name, false, "")
		)
		tag.reverseOfBounds = append(tag.reverseOfBounds, member.Ordinal)
		members = append(members, TableMember{
			Attributes:        member.Attributes,
			DataField:         name,
			PrivateDataField:  privateName,
			PresenceField:     name + "Present",
			Setter:            "Set" + name,
			Getter:            "Get" + name,
			GetterWithDefault: "Get" + name + "WithDefault",
			Haser:             "Has" + name,
			Clearer:           "Clear" + name,
			Type:              ty,
			FidlTag:           tag.String(),
		})
	}
	return Table{
		Attributes:    val.Attributes,
		Name:          c.compileCompoundIdentifier(val.Name, true, ""),
		InlineSizeOld: val.TypeShapeOld.InlineSize,
		AlignmentOld:  val.TypeShapeOld.Alignment,
		InlineSizeV1:  val.TypeShapeV1NoEE.InlineSize,
		AlignmentV1:   val.TypeShapeV1NoEE.Alignment,
		Members:       members,
	}
}

func (c *compiler) compileParameter(p types.Parameter) StructMember {
	ty, tag := c.compileType(p.Type)
	// TODO(fxb/7704): Remove special handling of requests/responses.
	offsetOld := p.FieldShapeOld.Offset - MessageHeaderSize
	offsetV1 := p.FieldShapeV1NoEE.Offset - MessageHeaderSize
	tag.reverseOfBounds = append(tag.reverseOfBounds, offsetOld)
	return StructMember{
		Type:        ty,
		Name:        c.compileIdentifier(p.Name, true, ""),
		PrivateName: c.compileIdentifier(p.Name, false, ""),
		FidlTag:     tag.String(),
		OffsetV1:    offsetV1,
	}
}

func (c *compiler) compileMethod(ifaceName types.EncodedCompoundIdentifier, val types.Method) Method {
	methodName := c.compileIdentifier(val.Name, true, "")
	r := Method{
		Attributes: val.Attributes,
		Name:       methodName,
		Ordinals: types.NewOrdinalsStep7(
			val,
			c.compileCompoundIdentifier(ifaceName, true, methodName+"Ordinal"),
			c.compileCompoundIdentifier(ifaceName, true, methodName+"GenOrdinal"),
		),
		EventExpectName: "Expect" + methodName,
		IsEvent:         !val.HasRequest && val.HasResponse,
		IsTransitional:  val.IsTransitional(),
	}
	if val.HasRequest {
		req := Struct{
			Name: c.compileCompoundIdentifier(ifaceName, false, methodName+"Request"),
			// We want just the size of the parameter array as a struct, not
			// including the message header size.
			InlineSizeOld: val.RequestTypeShapeOld.InlineSize - MessageHeaderSize,
			InlineSizeV1:  val.RequestTypeShapeV1NoEE.InlineSize - MessageHeaderSize,
		}
		for _, p := range val.Request {
			req.Members = append(req.Members, c.compileParameter(p))
		}
		r.Request = &req
	}
	if val.HasResponse {
		resp := Struct{
			Name: c.compileCompoundIdentifier(ifaceName, false, methodName+"Response"),
			// We want just the size of the parameter array as a struct, not
			// including the message header size.
			InlineSizeOld: val.ResponseTypeShapeOld.InlineSize - MessageHeaderSize,
			InlineSizeV1:  val.ResponseTypeShapeV1NoEE.InlineSize - MessageHeaderSize,
		}
		for _, p := range val.Response {
			resp.Members = append(resp.Members, c.compileParameter(p))
		}
		r.Response = &resp
	}
	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	var proxyType string
	switch val.Attributes.GetAttribute("Transport").Value {
	case "", "Channel":
		proxyType = "ChannelProxy"
	}
	r := Interface{
		Attributes:                  val.Attributes,
		Name:                        c.compileCompoundIdentifier(val.Name, true, ""),
		WithCtxName:                 c.compileCompoundIdentifier(val.Name, true, WithCtxSuffix),
		TransitionalBaseName:        c.compileCompoundIdentifier(val.Name, true, TransitionalBaseSuffix),
		TransitionalBaseWithCtxName: c.compileCompoundIdentifier(val.Name, true, WithCtxSuffix+TransitionalBaseSuffix),
		ProxyName:                   c.compileCompoundIdentifier(val.Name, true, ProxySuffix),
		ProxyType:                   proxyType,
		StubName:                    c.compileCompoundIdentifier(val.Name, true, StubSuffix),
		RequestName:                 c.compileCompoundIdentifier(val.Name, true, RequestSuffix),
		EventProxyName:              c.compileCompoundIdentifier(val.Name, true, EventProxySuffix),
		ServerName:                  c.compileCompoundIdentifier(val.Name, true, ServiceSuffix),
		ServiceNameConstant:         c.compileCompoundIdentifier(val.Name, true, ServiceNameSuffix),
		ServiceNameString:           val.GetServiceName(),
	}
	for _, v := range val.Methods {
		r.Methods = append(r.Methods, c.compileMethod(val.Name, v))
	}
	return r
}

func compileLibraryIdentifier(lib types.LibraryIdentifier) string {
	return "fidl/" + joinLibraryIdentifier(lib, "/")
}

func joinLibraryIdentifier(lib types.LibraryIdentifier, sep string) string {
	str := make([]string, len([]types.Identifier(lib)))
	for i, id := range lib {
		str[i] = string(id)
	}
	return strings.Join(str, sep)
}

// Compile translates parsed FIDL IR into golang backend IR for code generation.
func Compile(fidlData types.Root) Root {
	libraryName := types.ParseLibraryName(fidlData.Name)
	libraryPath := compileLibraryIdentifier(libraryName)

	// Collect all libraries.
	godeps := make(map[string]string)
	for _, v := range fidlData.Libraries {
		// Don't try to import yourself.
		if v.Name == fidlData.Name {
			continue
		}
		libComponents := types.ParseLibraryName(v.Name)
		path := compileLibraryIdentifier(libComponents)
		alias := changeIfReserved(
			types.Identifier(common.ToLowerCamelCase(
				joinLibraryIdentifier(libComponents, ""),
			)),
			"",
		)
		godeps[path] = alias
	}

	// Instantiate a compiler context.
	c := compiler{
		decls:           fidlData.DeclsWithDependencies(),
		library:         libraryName,
		libraryDeps:     godeps,
		usedLibraryDeps: make(map[string]string),
	}

	// Compile fidlData into r.
	r := Root{
		Name:        string(libraryName[len(libraryName)-1]),
		PackageName: libraryPath,
	}
	for _, v := range fidlData.Bits {
		r.Bits = append(r.Bits, c.compileBits(v))
	}
	for _, v := range fidlData.Consts {
		r.Consts = append(r.Consts, c.compileConst(v))
	}
	for _, v := range fidlData.Enums {
		r.Enums = append(r.Enums, c.compileEnum(v))
	}
	for _, v := range fidlData.Structs {
		r.Structs = append(r.Structs, c.compileStruct(v))
	}
	for _, v := range fidlData.Unions {
		r.XUnions = append(r.XUnions, c.compileXUnion(types.ConvertUnionToXUnion(v)))
	}
	for _, v := range fidlData.XUnions {
		r.XUnions = append(r.XUnions, c.compileXUnion(v))
	}
	for _, v := range fidlData.Tables {
		r.Tables = append(r.Tables, c.compileTable(v))
	}
	if len(fidlData.Structs) != 0 || len(fidlData.Interfaces) != 0 {
		c.usedLibraryDeps[BindingsPackage] = BindingsAlias
	}
	if len(fidlData.Interfaces) != 0 {
		c.usedLibraryDeps[SyscallZxPackage] = SyscallZxAlias
	}
	for _, v := range fidlData.Interfaces {
		r.Interfaces = append(r.Interfaces, c.compileInterface(v))
	}
	for path, alias := range c.usedLibraryDeps {
		r.Libraries = append(r.Libraries, Library{
			Path:  path,
			Alias: alias,
		})
	}
	// Sort the libraries according to Path.
	sort.Slice(r.Libraries, func(i, j int) bool {
		return strings.Compare(r.Libraries[i].Path, r.Libraries[j].Path) == -1
	})
	return r
}
