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

const (
	ProxySuffix   = "Interface"
	StubSuffix    = "Stub"
	RequestSuffix = "InterfaceRequest"

	MessageHeaderSize = 16
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
//
// Rather than representing handle nullability using a pointer indirection, we
// include it as part of the struct tag. For example, vector<handle?>:2 will
// have tag `fidl:"*,2"`.
type Tag struct {
	// MaxElems is the maximum number of elements a type is annotated with.
	MaxElems []*int

	// Nullable is whether the innermost type is nullable. This only applies
	// for handle types.
	Nullable bool
}

// String generates a string representation for the tag.
func (t *Tag) String() string {
	var elemsTag []string
	anyData := false
	if t.Nullable {
		elemsTag = append(elemsTag, "*")
		anyData = true
	}
	for _, elems := range t.MaxElems {
		if elems == nil {
			elemsTag = append(elemsTag, "")
			continue
		}
		anyData = true
		elemsTag = append(elemsTag, strconv.Itoa(*elems))
	}
	if !anyData {
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

// Interface represents a FIDL interface in terms of golang structures.
type Interface struct {
	// Name is the Golang name of the interface.
	Name string

	// ProxyName is the name of the proxy type for this FIDL interface.
	ProxyName string

	// StubName is the name of the stub type for this FIDL interface.
	StubName string

	// RequestName is the name of the interface request type for this FIDL interface.
	RequestName string

	// ServiceName is the service name for this FIDL interface.
	ServiceName string

	// Methods is a list of methods for this FIDL interface.
	Methods []Method
}

// Method represents a method of a FIDL interface in terms of golang structures.
type Method struct {
	// Ordinal is the ordinal for this method.
	Ordinal types.Ordinal

	// Name is the name of the Method, including the interface name as a prefix.
	Name string

	// Request represents a goland struct containing the request parameters.
	Request *Struct

	// Response represents an optional golang struct containing the response parameters.
	Response *Struct
}

// Root is the root of the golang backend IR structure.
//
// The golang backend IR structure is loosely modeled after an abstract syntax
// tree, and is used to generate golang code from templates.
type Root struct {
	// TODO(mknyszek): Support unions and constants.

	// Enums represents a list of FIDL enums represented as Go enums.
	Enums []Enum

	// Structs represents the list of FIDL structs represented as Go structs.
	Structs []Struct

	// Interfaces represents the list of FIDL interfaces represented as Go types.
	Interfaces []Interface
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

var handleTypes = map[types.HandleSubtype]string{
	// TODO(mknyszek): Add support here for process, thread, job, resource,
	// interrupt, eventpair, fifo, guest, and time once these are actually
	// supported in the Go runtime.
	types.Handle:  "_zx.Handle",
	types.Vmo:     "_zx.VMO",
	types.Channel: "_zx.Channel",
	types.Event:   "_zx.Event",
	types.Port:    "_zx.Port",
	types.Log:     "_zx.Log",
	types.Socket:  "_zx.Socket",
	types.Vmar:    "_zx.VMAR",
}

func exportIdentifier(name types.EncodedIdentifier) types.CompoundIdentifier {
	ci := types.ParseCompoundIdentifier(name)
	ci.Name = types.Identifier(common.ToUpperCamelCase(string(ci.Name)))
	return ci
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

func (_ *compiler) compileIdentifier(id types.Identifier, ext string) string {
	str := string(id)
	str = common.ToUpperCamelCase(str)
	return changeIfReserved(types.Identifier(str), ext)
}

func (_ *compiler) compileCompoundIdentifier(ei types.EncodedIdentifier, ext string) string {
	ci := exportIdentifier(ei)
	// TODO(kulakowski) Support more complicated names
	return changeIfReserved(ci.Name, ext)

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
	case types.HandleType:
		e, ok := handleTypes[val.HandleSubtype]
		if !ok {
			// Fall back onto a generic handle if we don't support that particular
			// handle subtype.
			e = handleTypes[types.Handle]
		}
		if val.Nullable {
			t.Nullable = true
		}
		r = Type(e)
	case types.RequestType:
		e := c.compileCompoundIdentifier(val.RequestSubtype, RequestSuffix)
		if val.Nullable {
			t.Nullable = true
		}
		r = Type(e)
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
		e := c.compileCompoundIdentifier(val.Identifier, "")
		declType, ok := c.decls[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		// TODO(mknyszek): Support unions.
		switch declType {
		case types.EnumDeclType:
			r = Type(e)
		case types.InterfaceDeclType:
			if val.Nullable {
				t.Nullable = true
			}
			r = Type(e + ProxySuffix)
		case types.StructDeclType:
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

func (c *compiler) compileEnumMember(val types.EnumMember) EnumMember {
	return EnumMember{
		Name:  c.compileIdentifier(val.Name, ""),
		Value: c.compileConstant(val.Value),
	}
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	r := Enum{
		Name: c.compileCompoundIdentifier(val.Name, ""),
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
		Name: c.compileIdentifier(val.Name, ""),
		Tag:  tag.String(),
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	r := Struct{
		Name:      c.compileCompoundIdentifier(val.Name, ""),
		Size:      val.Size,
		Alignment: val.Alignment,
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}
	return r
}

func (c *compiler) compileParameter(p types.Parameter) StructMember {
	ty, tag := c.compileType(p.Type)
	return StructMember{
		Type: ty,
		Name: c.compileIdentifier(p.Name, ""),
		Tag:  tag.String(),
	}
}

func (c *compiler) compileMethod(ifaceName types.EncodedIdentifier, val types.Method) Method {
	methodName := c.compileIdentifier(val.Name, "")
	r := Method{
		Name:    methodName,
		Ordinal: val.Ordinal,
	}
	if val.HasRequest {
		req := Struct{
			Name: c.compileCompoundIdentifier(ifaceName, methodName+"Request"),
			// We want just the size of the parameter array as a struct, not
			// including the message header size.
			Size: val.RequestSize - MessageHeaderSize,
		}
		for _, p := range val.Request {
			req.Members = append(req.Members, c.compileParameter(p))
		}
		r.Request = &req
	}
	if val.HasResponse {
		resp := Struct{
			Name: c.compileCompoundIdentifier(ifaceName, methodName+"Response"),
			// We want just the size of the parameter array as a struct, not
			// including the message header size.
			Size: val.ResponseSize - MessageHeaderSize,
		}
		for _, p := range val.Response {
			resp.Members = append(resp.Members, c.compileParameter(p))
		}
		r.Response = &resp
	}
	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		Name:        c.compileCompoundIdentifier(val.Name, ""),
		ProxyName:   c.compileCompoundIdentifier(val.Name, ProxySuffix),
		StubName:    c.compileCompoundIdentifier(val.Name, StubSuffix),
		RequestName: c.compileCompoundIdentifier(val.Name, RequestSuffix),
		ServiceName: val.GetAttribute("ServiceName"),
	}
	for _, v := range val.Methods {
		r.Methods = append(r.Methods, c.compileMethod(val.Name, v))
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
	for _, v := range fidlData.Interfaces {
		r.Interfaces = append(r.Interfaces, c.compileInterface(v))
	}
	return r
}
