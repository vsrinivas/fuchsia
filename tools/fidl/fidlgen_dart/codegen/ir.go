// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"log"
	"regexp"
	"strconv"
	"strings"
	"unicode"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Documented is embedded in structs for declarations that may hold documentation.
type Documented struct {
	Doc []string
}

// Type represents a FIDL datatype.
type Type struct {
	fidlgen.Type
	Decl          string // type in traditional bindings
	SyncDecl      string // type in async bindings when referring to traditional bindings
	AsyncDecl     string // type in async bindings when referring to async bindings
	OptionalDecl  string // type when the value is optional
	Nullable      bool
	declType      fidlgen.DeclType
	typedDataDecl string
	typeExpr      string
}

// Const represents a constant declaration.
type Const struct {
	Type  Type
	Name  string
	Value string
	Documented
}

// Enum represents an enum declaration.
type Enum struct {
	fidlgen.Enum

	Name       string
	Members    []EnumMember
	TypeSymbol string
	TypeExpr   string
	Documented
}

// EnumMember represents a member of an enum declaration.
type EnumMember struct {
	fidlgen.EnumMember

	Name  string
	Value string
	Documented
}

// Bits represents a bits declaration.
type Bits struct {
	fidlgen.Bits

	Name       string
	Members    []BitsMember
	TypeSymbol string
	TypeExpr   string
	Mask       uint64
	Documented
}

// BitsMember represents a member of a bits declaration.
type BitsMember struct {
	Name  string
	Value string
	Documented
}

type PayloadableName struct {
	Name string
}

// AsParameters default behavior for all payloadable types (Struct, Table, and
// Union) is to simple make a single "parameter" of the type in question (aka
// the "unflattend" representation), always named `value`. Note that *Struct has
// its own override of `AsParameters` which does in fact perform flattening.
func (u *PayloadableName) AsParameters(ty Type) []Parameter {
	return []Parameter{
		{
			Type:       ty,
			TypeSymbol: ty.typeExpr,
			Name:       unflattenedPayloadName,
			Flattened:  false,
			typeExpr:   fmt.Sprintf("$fidl.MemberType<%s>(type: %s, offset: 0)", ty.Decl, ty.typeExpr),
		},
	}
}

// Union represents a union declaration.
type Union struct {
	fidlgen.Union
	PayloadableName
	TagName       string
	Members       []UnionMember
	TypeSymbol    string
	TypeExpr      string
	OptTypeSymbol string
	OptTypeExpr   string
	Documented
}

// UnionMember represents a member of a Union declaration.
type UnionMember struct {
	Ordinal  uint64
	Type     Type
	Name     string
	CtorName string
	Tag      string
	Documented
}

// Struct represents a struct declaration.
type Struct struct {
	fidlgen.Struct
	PayloadableName
	Members          []StructMember
	Paddings         []StructPadding
	TypeSymbol       string
	TypeExpr         string
	HasNullableField bool
	Documented
	isEmptyStruct bool
}

// AsParameters produces a "flattened" representation of the Struct's members,
// so that they may be used in certain contexts, like rendering method
// signatures. Thus, if we have a FIDL payload like:
//
//	type MyStruct = struct {
//	  a bool;
//	  b int8;
//	};
//	protocol MyProtocol {
//	  MyMethod(MyStruct);
//	};
//
// It will produce a "flattened" Dart function signature like:
//
//	Future<void> myMethod(bool a, int b);
//
// Rather than an "unflattened" one like:
//
//	Future<void> myMethod(MyStruct value);
func (s *Struct) AsParameters(_ Type) []Parameter {
	var parameters []Parameter
	if s.isEmptyStruct {
		return parameters
	}
	for _, v := range s.Members {
		parameters = append(parameters, Parameter{
			Type:       v.Type,
			TypeSymbol: v.typeExpr,
			Name:       v.Name,
			Flattened:  true,
			typeExpr:   v.typeExpr,
		})
	}
	return parameters
}

// StructMember represents a member of a struct declaration.
type StructMember struct {
	Type         Type
	TypeSymbol   string
	Name         string
	DefaultValue string
	OffsetV2     int
	typeExpr     string
	Documented
}

type StructPadding struct {
	OffsetV2  int
	PaddingV2 int
}

// Table represents a table declaration.
type Table struct {
	fidlgen.Table
	PayloadableName
	Members    []TableMember
	TypeSymbol string
	TypeExpr   string
	Documented
}

// TableMember represents a member of a table declaration.
type TableMember struct {
	Ordinal      int
	Index        int
	Type         Type
	Name         string
	DefaultValue string
	typeExpr     string
	Documented
}

// Protocol represents an protocol declaration.
type Protocol struct {
	fidlgen.Protocol
	Name        string
	ServiceData string
	ProxyName   string
	BindingName string
	ServerName  string
	EventsName  string
	Methods     []Method
	HasEvents   bool
	Documented
}

// Parameter represents a method request/response payload parameter. In the
// "unflattened" case (valid for union/table payloads), there is always one
// parameter per payload, representing the entire union/table being transported.
// In the "flattened" case, each parameter represents a single member of the
// struct in question.
type Parameter struct {
	Type       Type
	TypeSymbol string
	Name       string
	Flattened  bool
	typeExpr   string
}

type MethodResponse struct {
	// WireParameters represent the parameters of the top level response struct
	// that is sent on the wire
	WireParameters []Parameter
	// MethodParameters represent the parameters that the user interacts with
	// when using generated methods. When HasError is false, this is the same as
	// WireParameters. When HasError is true, MethodParameters corresponds to the
	// fields of a successful response.
	MethodParameters  []Parameter
	HasError          bool
	HasTransportError bool
	ResultTypeName    string
	ResultTypeTagName string
	ValueType         Type
	ErrorType         Type
}

// Method represents a method declaration within an protocol declaration.
type Method struct {
	fidlgen.Method
	Ordinal     uint64
	OrdinalName string
	Name        string
	HasRequest  bool
	Request     []Parameter
	HasResponse bool
	Response    MethodResponse
	// AsyncResponseClass is a named tuple that wraps the MethodParameters of
	// a response, and is only generated when there is more than one parameter
	AsyncResponseClass string
	AsyncResponseType  string
	CallbackType       string
	TypeSymbol         string
	TypeExpr           string
	Transitional       bool
	Documented
}

// ResponseMessageType is the Dart type returned by the "DecodeResponse" template.
func (m Method) ResponseMessageType() string {
	if !m.HasResponse {
		return "void"
	}
	if m.Response.HasError || m.Response.HasTransportError {
		return m.Response.ResultTypeName
	}
	return m.AsyncResponseClass
}

// AsyncResultCompleter provides the appropriate value to $completer.complete
// based on the types of the arguments and whether the method uses error syntax.
func (m Method) AsyncResponseCompleter() string {
	if !m.HasResponse || (!m.Response.HasError && !m.Response.HasTransportError) {
		return "$completer.complete($response);"
	}

	var out strings.Builder
	fmt.Fprintf(&out, "if ($response.$tag == %s.response) {\n", m.Response.ResultTypeTagName)
	out.WriteString("$completer.complete(")
	switch len(m.Response.MethodParameters) {
	case 0:
		out.WriteString("null);")
	case 1:
		param := m.Response.MethodParameters[0]
		if param.Flattened {
			fmt.Fprintf(&out, "$response.response!.%s);", param.Name)
		} else {
			out.WriteString("$response.response!);")
		}
	default:
		out.WriteString(m.AsyncResponseClass + "(")
		for _, p := range m.Response.MethodParameters {
			fmt.Fprintf(&out, "$response.response!.%s, ", p.Name)
		}
		out.WriteString("));")
	}

	if m.Response.HasError && m.Response.HasTransportError {
		fmt.Fprintf(&out, "} else if ($response.$tag == %s.err) {", m.Response.ResultTypeTagName)
		out.WriteString(`
			$completer.completeError($fidl.MethodException($response.err));
		} else {
			$completer.completeError($fidl.FidlError.fromTransportErr($response.transportErr!));
		}`)
	} else if m.Response.HasError {
		out.WriteString(`} else {
			$completer.completeError($fidl.MethodException($response.err));
		}`)
	} else { // m.Response.HasTransportError
		out.WriteString(`} else {
			$completer.completeError($fidl.FidlError.fromTransportErr($response.transportErr!));
		}`)
	}
	return out.String()
}

// AsyncResultResponse produces code to convert the result of an error-syntax
// method into the appropriate result union.
func (m Method) AsyncResultResponse() string {
	if !m.HasResponse || (!m.Response.HasError && !m.Response.HasTransportError) {
		return ""
	}

	var out strings.Builder
	fmt.Fprintf(&out, "%s.withResponse(\n", m.Response.ResultTypeName)
	switch len(m.Response.MethodParameters) {
	case 0:
		fmt.Fprintf(&out, `%s()`, m.Response.ValueType.Decl)
	case 1:
		param := m.Response.MethodParameters[0]
		if param.Flattened {
			fmt.Fprintf(&out, "%s(%s: $responseValue)", m.Response.ValueType.Decl, param.Name)
		} else {
			out.WriteString("$responseValue")
		}
	default:
		fmt.Fprintf(&out, `%s(`, m.Response.ValueType.Decl)
		for _, p := range m.Response.MethodParameters {
			fmt.Fprintf(&out, "%s: $responseValue.%s, ", p.Name, p.Name)
		}
		out.WriteString(")")
	}

	out.WriteString(")")
	return out.String()
}

// AddEventResponse provides the appropriate value to the EventStreamController
// for an event, based on whether the event uses error syntax.
func (m Method) AddEventResponse() string {
	if !m.HasResponse || !m.Response.HasError {
		return fmt.Sprintf("_%sEventStreamController.add($response);", m.Name)
	}

	var out strings.Builder
	fmt.Fprintf(&out, `if ($response.$tag == %s.response) {
		_%sEventStreamController.add(`, m.Response.ResultTypeTagName, m.Name)
	switch len(m.Response.MethodParameters) {
	case 0:
		out.WriteString("null);")
	case 1:
		param := m.Response.MethodParameters[0]
		if param.Flattened {
			fmt.Fprintf(&out, "$response.response!.%s);", param.Name)
		} else {
			out.WriteString("$response.response!);")
		}
	default:
		out.WriteString(m.AsyncResponseClass + "(")
		for _, p := range m.Response.MethodParameters {
			fmt.Fprintf(&out, "$response.response!.%s, ", p.Name)
		}
		out.WriteString("));")
	}

	fmt.Fprintf(&out, `} else {
		_%sEventStreamController.addError($fidl.MethodException($response.err));
	}`, m.Name)
	return out.String()
}

// MessageHeaderStrictness setting to use for encodeMessageHeader arguments.
func (m Method) MessageHeaderStrictness() string {
	if m.IsStrict() {
		return "$fidl.CallStrictness.strict"
	}
	return "$fidl.CallStrictness.flexible"
}

// Import describes another FIDL library that will be imported.
type Import struct {
	URL       string
	LocalName string
	AsyncURL  string
}

// Root holds all of the declarations for a FIDL library.
type Root struct {
	LibraryName     string
	Imports         []Import
	Consts          []Const
	Enums           []Enum
	Bits            []Bits
	Protocols       []Protocol
	Structs         []Struct
	Tables          []Table
	Unions          []Union
	ExternalStructs []Struct
}

type nameContext struct{ fidlgen.NameContext }

func newNameContext() nameContext {
	return nameContext{fidlgen.NewNameContext()}
}

func (ctx nameContext) changeIfReserved(name string) string {
	if ctx.IsReserved(name) {
		return name + "$"
	}
	return name
}

var (
	// Name of a bits member
	bitsMemberContext = newNameContext()
	// Name of an enum member
	enumMemberContext = newNameContext()
	// Name of a struct member
	structMemberContext = newNameContext()
	// Name of a table member
	tableMemberContext = newNameContext()
	// Name of a union member
	unionMemberContext = newNameContext()
	// Tag of a union member
	unionMemberTagContext = newNameContext()
	// Name of a constant
	constantContext = newNameContext()
	// Name of a top-level declaration (other than a constant)
	declarationContext = newNameContext()
	// Name of a method
	methodContext = newNameContext()
	// For unflattened payloads, always use the same name in function signatures.
	unflattenedPayloadName = "payload"
)

func init() {
	// Reserve names that are universally reserved:
	for _, ctx := range []nameContext{
		enumMemberContext, structMemberContext, tableMemberContext,
		unionMemberContext, unionMemberTagContext, constantContext,
		declarationContext, methodContext, bitsMemberContext,
	} {
		ctx.ReserveNames([]string{"assert", "async", "await", "break", "case",
			"catch", "class", "const", "continue", "default", "do", "else",
			"enum", "extends", "false", "final", "finally", "for", "if", "in",
			"is", "new", "null", "override", "rethrow", "return", "String",
			"super", "switch", "this", "throw", "true", "try", "var", "void",
			"while", "with", "yield"})
	}

	constantContext.ReserveNames([]string{"dynamic", "int", "num"})
	declarationContext.ReserveNames([]string{"List", "Map", "Never", "None",
		"Null", "Object", "Some"})
	methodContext.ReserveNames([]string{"dynamic", "hashCode", "int",
		"noSuchMethod", "num", "runtimeType", "toString"})
	bitsMemberContext.ReserveNames([]string{"dynamic", "hashCode", "int",
		"toString"})
	enumMemberContext.ReserveNames([]string{"bool", "dynamic", "hashCode",
		"int", "noSuchMethod", "num", "runtimeType", "toString"})
	structMemberContext.ReserveNames([]string{"bool", "double", "dynamic",
		"hashCode", "int", "noSuchMethod", "num", "runtimeType", "toString"})
	tableMemberContext.ReserveNames([]string{"bool", "double", "dynamic",
		"hashCode", "int", "noSuchMethod", "num", "runtimeType", "toString"})
	unionMemberContext.ReserveNames([]string{"bool", "dynamic", "hashCode", "int",
		"noSuchMethod", "num", "runtimeType", "toString"})
	unionMemberTagContext.ReserveNames([]string{"index", "values"})
}

var declForPrimitiveType = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Bool:    "bool",
	fidlgen.Int8:    "int",
	fidlgen.Int16:   "int",
	fidlgen.Int32:   "int",
	fidlgen.Int64:   "int",
	fidlgen.Uint8:   "int",
	fidlgen.Uint16:  "int",
	fidlgen.Uint32:  "int",
	fidlgen.Uint64:  "int",
	fidlgen.Float32: "double",
	fidlgen.Float64: "double",
}

var typedDataDecl = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Int8:    "Int8List",
	fidlgen.Int16:   "Int16List",
	fidlgen.Int32:   "Int32List",
	fidlgen.Int64:   "Int64List",
	fidlgen.Uint8:   "Uint8List",
	fidlgen.Uint16:  "Uint16List",
	fidlgen.Uint32:  "Uint32List",
	fidlgen.Uint64:  "Uint64List",
	fidlgen.Float32: "Float32List",
	fidlgen.Float64: "Float64List",
}

var typeForPrimitiveSubtype = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Bool:    "BoolType",
	fidlgen.Int8:    "Int8Type",
	fidlgen.Int16:   "Int16Type",
	fidlgen.Int32:   "Int32Type",
	fidlgen.Int64:   "Int64Type",
	fidlgen.Uint8:   "Uint8Type",
	fidlgen.Uint16:  "Uint16Type",
	fidlgen.Uint32:  "Uint32Type",
	fidlgen.Uint64:  "Uint64Type",
	fidlgen.Float32: "Float32Type",
	fidlgen.Float64: "Float64Type",
}

var declForInternalType = map[fidlgen.InternalSubtype]string{
	fidlgen.TransportErr: "$fidl.TransportErr",
}

var typeForInternalSubtype = map[fidlgen.InternalSubtype]string{
	fidlgen.TransportErr: "TransportErrType",
}

func docStringLink(nameWithBars string) string {
	return fmt.Sprintf("[%s]", nameWithBars[1:len(nameWithBars)-1])
}

var reLink = regexp.MustCompile("\\|([^\\|]+)\\|")

// TODO(pascallouis): rethink how we depend on the fidlgen package.
type Annotated interface {
	LookupAttribute(fidlgen.Identifier) (fidlgen.Attribute, bool)
}

func docString(node Annotated) Documented {
	attribute, ok := node.LookupAttribute("doc")
	if !ok {
		return Documented{nil}
	}
	value, ok := attribute.LookupArg("value")
	if !ok {
		return Documented{nil}
	}
	var docs []string
	lines := strings.Split(value.ValueString(), "\n")
	if len(lines[len(lines)-1]) == 0 {
		// Remove the blank line at the end
		lines = lines[:len(lines)-1]
	}
	for _, line := range lines {
		// Turn |something| into [something]
		line = reLink.ReplaceAllStringFunc(line, docStringLink)
		docs = append(docs, line)
	}
	return Documented{docs}
}

func formatBool(val bool) string {
	return strconv.FormatBool(val)
}

func formatInt(val *int) string {
	if val == nil {
		return "null"
	}
	return strconv.Itoa(*val)
}

func formatParameterList(params []Parameter) string {
	if len(params) == 0 {
		return "[]"
	}

	lines := []string{}

	for _, p := range params {
		lines = append(lines, fmt.Sprintf("      %s,\n", p.typeExpr))
	}

	return fmt.Sprintf("<$fidl.MemberType>[\n%s    ]", strings.Join(lines, ""))
}

func formatTableMemberList(members []TableMember) string {
	if len(members) == 0 {
		return "[]"
	}

	lines := []string{}
	for _, member := range members {
		for len(lines) < member.Ordinal-1 {
			lines = append(lines, fmt.Sprintf("    null,\n"))
		}
		lines = append(lines, fmt.Sprintf("    %s,\n", member.Type.typeExpr))
	}

	return fmt.Sprintf("[\n%s  ]", strings.Join(lines, ""))
}

func formatUnionMemberList(members []UnionMember) string {
	if len(members) == 0 {
		return "<int, $fidl.FidlType>{}"
	}

	var lines []string
	for _, v := range members {
		lines = append(lines, fmt.Sprintf("    %d: %s,\n", v.Ordinal, v.Type.typeExpr))
	}

	return fmt.Sprintf("<int, $fidl.FidlType>{\n%s  }", strings.Join(lines, ""))
}

func formatLibraryName(library fidlgen.LibraryIdentifier) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return strings.Join(parts, "_")
}

func typeExprForPrimitiveSubtype(val fidlgen.PrimitiveSubtype) string {
	t, ok := typeForPrimitiveSubtype[val]
	if !ok {
		log.Fatal("Unknown primitive subtype: ", val)
	}
	return fmt.Sprintf("$fidl.%s()", t)
}

func typeExprForInternalSubtype(val fidlgen.InternalSubtype) string {
	t, ok := typeForInternalSubtype[val]
	if !ok {
		log.Fatal("Unknown internal subtype: ", val)
	}
	return fmt.Sprintf("$fidl.%s()", t)
}

func libraryPrefix(library fidlgen.LibraryIdentifier) string {
	return fmt.Sprintf("lib$%s", formatLibraryName(library))
}

type compiler struct {
	Root
	decls          fidlgen.DeclInfoMap
	library        fidlgen.LibraryIdentifier
	typesRoot      fidlgen.Root
	paramableTypes map[fidlgen.EncodedCompoundIdentifier]Parameterizer
}

// Parameterizer should be implemented by all "payloadable" layout types (that
// is, Struct, Table, and Union) so that method signatures and encoding/decoding
// type lists may be built out for each of them.
type Parameterizer interface {
	AsParameters(Type) []Parameter
}

// Assert that parameterizable layouts conform to the payloader interface.
var _ = []Parameterizer{
	(*PayloadableName)(nil),
	(*Table)(nil),
	(*Struct)(nil),
	(*Union)(nil),
}

func (c *compiler) compileParameters(t *fidlgen.Type) []Parameter {
	ty := c.compileType(*t)
	val, ok := c.paramableTypes[ty.Identifier]
	if !ok {
		panic(fmt.Sprintf("Unknown request/response layout: %s", ty.Identifier))
	}
	return val.AsParameters(ty)
}

func (c *compiler) typeExprForMethod(val fidlgen.Method, request []Parameter, response []Parameter, name string) string {
	var (
		requestSizeV2  = 0
		responseSizeV2 = 0
	)
	if val.RequestPayload != nil {
		requestSizeV2 = val.RequestPayload.TypeShapeV2.InlineSize
	}
	if val.ResponsePayload != nil {
		responseSizeV2 = val.ResponsePayload.TypeShapeV2.InlineSize
	}

	// request/response and requestInlineSize/responseInlineSize are null/0 for
	// both empty payloads and when there are no request/responses. The HasRequest
	// and HasResponse fields are used to distinguish between these two cases
	// during codegen.
	return fmt.Sprintf(`$fidl.MethodType(
	    request: %s,
		response: %s,
		name: r"%s",
		requestInlineSizeV2: %d,
		responseInlineSizeV2: %d,
	  )`, formatParameterList(request), formatParameterList(response), name,
		requestSizeV2, responseSizeV2)
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

func (c *compiler) typeSymbolForCompoundIdentifier(ident fidlgen.CompoundIdentifier) string {
	return c._typeSymbolForCompoundIdentifier(ident, "Type")
}

func (c *compiler) optTypeSymbolForCompoundIdentifier(ident fidlgen.CompoundIdentifier) string {
	return c._typeSymbolForCompoundIdentifier(ident, "OptType")
}

func (c *compiler) _typeSymbolForCompoundIdentifier(ident fidlgen.CompoundIdentifier, suffix string) string {
	t := fmt.Sprintf("k%s_%s", ident.Name, suffix)
	if c.inExternalLibrary(ident) {
		return fmt.Sprintf("%s.%s", libraryPrefix(ident.Library), t)
	}
	return t
}

func (c *compiler) compileUpperCamelIdentifier(val fidlgen.Identifier, context nameContext) string {
	return context.changeIfReserved(fidlgen.ToUpperCamelCase(string(val)))
}

func (c *compiler) compileLowerCamelIdentifier(val fidlgen.Identifier, context nameContext) string {
	return context.changeIfReserved(fidlgen.ToLowerCamelCase(string(val)))
}

func (c *compiler) compileCompoundIdentifier(val fidlgen.CompoundIdentifier, context nameContext) string {
	strs := []string{}
	if c.inExternalLibrary(val) {
		strs = append(strs, libraryPrefix(val.Library))
	}
	strs = append(strs, context.changeIfReserved(string(val.Name)))
	if val.Member != "" {
		strs = append(strs, context.changeIfReserved(string(val.Member)))
	}
	return strings.Join(strs, ".")
}

func (c *compiler) compileUpperCamelCompoundIdentifier(val fidlgen.CompoundIdentifier, ext string, context nameContext) string {
	str := fidlgen.ToUpperCamelCase(string(val.Name)) + ext
	val.Name = fidlgen.Identifier(str)
	return c.compileCompoundIdentifier(val, context)
}

func (c *compiler) compileLowerCamelCompoundIdentifier(val fidlgen.CompoundIdentifier, ext string, context nameContext) string {
	str := fidlgen.ToLowerCamelCase(string(val.Name)) + ext
	val.Name = fidlgen.Identifier(str)
	return c.compileCompoundIdentifier(val, context)
}

func (c *compiler) compileConstantIdentifier(val fidlgen.CompoundIdentifier, context nameContext) string {
	if val.Member != "" {
		// val.Name here is the type.
		// Format: Type.memberIdentifier
		val.Name = fidlgen.Identifier(fidlgen.ToUpperCamelCase(string(val.Name)))
		val.Member = fidlgen.Identifier(fidlgen.ToLowerCamelCase(string(val.Member)))
	} else {
		// Format: valueIdentifier
		val.Name = fidlgen.Identifier(fidlgen.ToLowerCamelCase(string(val.Name)))
	}
	return c.compileCompoundIdentifier(val, context)
}

func (c *compiler) compileLiteral(val fidlgen.Literal) string {
	switch val.Kind {
	case fidlgen.StringLiteral:
		var b strings.Builder
		b.WriteRune('\'')
		for _, r := range val.Value {
			switch r {
			case '\\':
				b.WriteString(`\\`)
			case '\'':
				b.WriteString(`\'`)
			case '\n':
				b.WriteString(`\n`)
			case '\r':
				b.WriteString(`\r`)
			case '\t':
				b.WriteString(`\t`)
			case '$':
				b.WriteString(`\$`)
			default:
				if unicode.IsPrint(r) {
					b.WriteRune(r)
				} else {
					b.WriteString(fmt.Sprintf(`\u{%x}`, r))
				}
			}
		}
		b.WriteRune('\'')
		return b.String()
	case fidlgen.NumericLiteral:
		// TODO(fxbug.dev/7810): Once we expose resolved constants for defaults, e.g.
		// in structs, we will not need ignore hex and binary values.
		if strings.HasPrefix(val.Value, "0x") || strings.HasPrefix(val.Value, "0b") {
			return val.Value
		}

		// No special handling of floats.
		if strings.ContainsRune(val.Value, '.') {
			return val.Value
		}

		// For numbers larger than int64, they must be emitted as hex numbers.
		// We simply do this for all positive numbers.
		if strings.HasPrefix(val.Value, "-") {
			return val.Value
		}
		num, err := strconv.ParseUint(val.Value, 10, 64)
		if err != nil {
			panic(fmt.Sprintf("JSON IR contains invalid numeric literal: %s", val.Value))
		}
		return fmt.Sprintf("%#x", num)
	case fidlgen.BoolLiteral:
		return val.Value
	case fidlgen.DefaultLiteral:
		return "default"
	default:
		log.Fatal("Unknown literal kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val fidlgen.Constant, t *Type) string {
	switch val.Kind {
	case fidlgen.IdentifierConstant:
		return c.compileConstantIdentifier(val.Identifier.Parse(), constantContext)
	case fidlgen.LiteralConstant:
		return c.compileLiteral(*val.Literal)
	case fidlgen.BinaryOperator:
		if t.declType == fidlgen.BitsDeclType {
			return fmt.Sprintf("%s._(%s)", t.Decl, val.Value)
		}
		return fmt.Sprintf("%s", val.Value)
	default:
		log.Fatal("Unknown constant kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val fidlgen.PrimitiveSubtype) string {
	if t, ok := declForPrimitiveType[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type: ", val)
	return ""
}

func (c *compiler) compileInternalSubtype(val fidlgen.InternalSubtype) string {
	if t, ok := declForInternalType[val]; ok {
		return t
	}
	log.Fatal("Unknown internal type: ", val)
	return ""
}

func (c *compiler) compileType(val fidlgen.Type) Type {
	nullablePrefix := ""
	if val.Nullable {
		nullablePrefix = "Nullable"
	}
	r := Type{
		Type: val,
	}
	r.Nullable = val.Nullable
	switch val.Kind {
	case fidlgen.ArrayType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
			r.SyncDecl = r.Decl
			r.AsyncDecl = r.Decl
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
			r.SyncDecl = fmt.Sprintf("List<%s>", t.SyncDecl)
			r.AsyncDecl = fmt.Sprintf("List<%s>", t.AsyncDecl)

		}
		elementStr := fmt.Sprintf("element: %s", t.typeExpr)
		elementCountStr := fmt.Sprintf("elementCount: %s", formatInt(val.ElementCount))
		r.typeExpr = fmt.Sprintf("$fidl.ArrayType<%s, %s>(%s, %s)", t.Decl, r.Decl, elementStr, elementCountStr)
	case fidlgen.VectorType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
			r.SyncDecl = r.Decl
			r.AsyncDecl = r.Decl
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
			r.SyncDecl = fmt.Sprintf("List<%s>", t.SyncDecl)
			r.AsyncDecl = fmt.Sprintf("List<%s>", t.AsyncDecl)
		}
		elementStr := fmt.Sprintf("element: %s", t.typeExpr)
		maybeElementCountStr := fmt.Sprintf("maybeElementCount: %s", formatInt(val.ElementCount))
		r.typeExpr = fmt.Sprintf("$fidl.%sVectorType<%s, %s>(%s, %s)",
			nullablePrefix, t.Decl, r.Decl, elementStr, maybeElementCountStr)
	case fidlgen.StringType:
		r.Decl = "String"
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		r.typeExpr = fmt.Sprintf("$fidl.%sStringType(maybeElementCount: %s)",
			nullablePrefix, formatInt(val.ElementCount))
	case fidlgen.HandleType:
		var subtype string
		switch val.HandleSubtype {
		case "channel":
			subtype = "Channel"
		case "eventpair":
			subtype = "EventPair"
		case "socket":
			subtype = "Socket"
		case "vmo":
			subtype = "Vmo"
		default:
			subtype = "Handle"
		}
		r.Decl = "$zircon." + subtype
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		baseType := fmt.Sprintf("$fidl.%sType(objectType: %d, rights: %d)",
			subtype, val.ObjType, val.HandleRights)
		if val.Nullable {
			r.typeExpr = fmt.Sprintf("$fidl.NullableHandleType(%s)", baseType)
		} else {
			r.typeExpr = baseType
		}
	case fidlgen.RequestType:
		compound := val.RequestSubtype.Parse()
		t := c.compileUpperCamelCompoundIdentifier(compound, "", declarationContext)
		r.Decl = fmt.Sprintf("$fidl.InterfaceRequest<%s>", t)
		if c.inExternalLibrary(compound) {
			r.SyncDecl = fmt.Sprintf("$fidl.InterfaceRequest<sync$%s>", t)
		} else {
			r.SyncDecl = fmt.Sprintf("$fidl.InterfaceRequest<$sync.%s>", t)
		}
		r.AsyncDecl = r.Decl
		r.typeExpr = fmt.Sprintf("$fidl.%sInterfaceRequestType<%s>()",
			nullablePrefix, t)
	case fidlgen.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		r.typedDataDecl = typedDataDecl[val.PrimitiveSubtype]
		r.typeExpr = typeExprForPrimitiveSubtype(val.PrimitiveSubtype)
	case fidlgen.InternalType:
		r.Decl = c.compileInternalSubtype(val.InternalSubtype)
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		r.typeExpr = typeExprForInternalSubtype(val.InternalSubtype)
	case fidlgen.IdentifierType:
		compound := val.Identifier.Parse()
		t := c.compileUpperCamelCompoundIdentifier(compound, "", declarationContext)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		r.declType = declInfo.Type
		switch r.declType {
		case fidlgen.ConstDeclType:
			fallthrough
		case fidlgen.EnumDeclType:
			fallthrough
		case fidlgen.BitsDeclType:
			fallthrough
		case fidlgen.StructDeclType:
			fallthrough
		case fidlgen.TableDeclType:
			fallthrough
		case fidlgen.UnionDeclType:
			r.Decl = t
			if c.inExternalLibrary(compound) {
				r.SyncDecl = fmt.Sprintf("sync$%s", t)

			} else {
				r.SyncDecl = fmt.Sprintf("$sync.%s", t)
			}
			r.AsyncDecl = r.SyncDecl
			if val.Nullable {
				switch r.declType {
				case fidlgen.UnionDeclType:
					r.typeExpr = c.optTypeSymbolForCompoundIdentifier(val.Identifier.Parse())
				default:
					r.typeExpr = fmt.Sprintf("$fidl.PointerType<%s>(element: %s)",
						t, c.typeSymbolForCompoundIdentifier(val.Identifier.Parse()))
				}
			} else {
				r.typeExpr = c.typeSymbolForCompoundIdentifier(val.Identifier.Parse())
			}
		case fidlgen.ProtocolDeclType:
			r.Decl = fmt.Sprintf("$fidl.InterfaceHandle<%s>", t)
			if c.inExternalLibrary(compound) {
				r.SyncDecl = fmt.Sprintf("$fidl.InterfaceHandle<sync$%s>", t)
			} else {
				r.SyncDecl = fmt.Sprintf("$fidl.InterfaceHandle<$sync.%s>", t)
			}
			r.AsyncDecl = r.Decl
			r.typeExpr = fmt.Sprintf("$fidl.%sInterfaceHandleType<%s>()", nullablePrefix, t)
		default:
			log.Fatal("Unknown declaration type: ", r.declType)
		}
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}
	if r.AsyncDecl == "" {
		log.Fatalf("No AsyncDecl for %s", r.Decl)
	}
	if r.SyncDecl == "" {
		log.Fatalf("No SyncDecl for %s", r.Decl)
	}
	if r.Nullable {
		r.Decl = r.Decl + "?"
		r.OptionalDecl = r.Decl
	} else {
		r.OptionalDecl = r.Decl + "?"
	}
	return r
}

func (c *compiler) compileConst(val fidlgen.Const) Const {
	t := c.compileType(val.Type)
	r := Const{
		Type:       t,
		Name:       c.compileLowerCamelCompoundIdentifier(val.Name.Parse(), "", constantContext),
		Value:      c.compileConstant(val.Value, &t),
		Documented: docString(val),
	}
	return r
}

func (c *compiler) membersAsMapToNull(members []fidlgen.EnumMember) string {
	var values []string
	for _, member := range members {
		values = append(values, fmt.Sprintf("%s:null", c.compileConstant(member.Value, nil)))
	}
	return strings.Join(values, ",")
}

func (c *compiler) compileEnum(val fidlgen.Enum) Enum {
	ci := val.Name.Parse()
	n := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
	e := Enum{
		Enum:       val,
		Name:       n,
		TypeSymbol: c.typeSymbolForCompoundIdentifier(ci),
		TypeExpr: fmt.Sprintf("$fidl.EnumType<%s>(type: %s, values: {%s}, ctor: %s._ctor)",
			n, typeExprForPrimitiveSubtype(val.Type), c.membersAsMapToNull(val.Members), n),
		Documented: docString(val),
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			EnumMember: v,
			Name:       c.compileLowerCamelIdentifier(v.Name, enumMemberContext),
			Value:      c.compileConstant(v.Value, nil),
			Documented: docString(v),
		})
	}
	return e
}

func (c *compiler) compileBits(val fidlgen.Bits) Bits {
	ci := val.Name.Parse()
	n := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
	if val.Type.Kind != fidlgen.PrimitiveType {
		panic("unexpected, only primitives are allowed for bits declarations")
	}
	subtype := val.Type.PrimitiveSubtype

	// TODO(fxbug.dev/59044): Mask should be an int
	maskVal, err := strconv.ParseUint(val.Mask, 10, 64)
	if err != nil {
		panic(fmt.Sprintf("JSON IR contains invalid mask value: %s", val.Mask))
	}

	b := Bits{
		Bits:       val,
		Name:       n,
		TypeSymbol: c.typeSymbolForCompoundIdentifier(ci),
		TypeExpr:   fmt.Sprintf("$fidl.BitsType<%s>(type: %s, ctor: %s._ctor)", n, typeExprForPrimitiveSubtype(subtype), n),
		Mask:       maskVal,
		Documented: docString(val),
	}
	for _, v := range val.Members {
		b.Members = append(b.Members, BitsMember{
			Name:       c.compileLowerCamelIdentifier(v.Name, bitsMemberContext),
			Value:      c.compileConstant(v.Value, nil),
			Documented: docString(v),
		})
	}
	return b
}

func (c *compiler) compileMethodResponse(method fidlgen.Method) MethodResponse {
	if method.HasError || method.HasTransportError() {
		response := MethodResponse{
			WireParameters:    c.compileParameters(method.ResponsePayload),
			MethodParameters:  c.compileParameters(method.ValueType),
			HasError:          method.HasError,
			HasTransportError: method.HasTransportError(),
			ResultTypeName:    c.compileUpperCamelCompoundIdentifier(method.ResultType.Identifier.Parse(), "", declarationContext),
			ResultTypeTagName: c.compileUpperCamelCompoundIdentifier(method.ResultType.Identifier.Parse(), "Tag", declarationContext),
			ValueType:         c.compileType(*method.ValueType),
		}
		if method.HasError {
			response.ErrorType = c.compileType(*method.ErrorType)
		}
		return response
	}

	response := c.compileParameters(method.ResponsePayload)
	return MethodResponse{
		WireParameters:   response,
		MethodParameters: response,
	}
}

func (c *compiler) compileMethod(val fidlgen.Method, protocol Protocol, fidlProtocol fidlgen.Protocol) Method {
	var (
		name               = c.compileLowerCamelIdentifier(val.Name, methodContext)
		request            []Parameter
		response           MethodResponse
		asyncResponseClass string
		asyncResponseType  string
	)
	if val.RequestPayload != nil {
		request = c.compileParameters(val.RequestPayload)
	}
	if val.ResponsePayload != nil {
		response = c.compileMethodResponse(val)
	}

	if val.HasResponse {
		switch len(response.MethodParameters) {
		case 0:
			asyncResponseType = "void"
		case 1:
			asyncResponseType = response.MethodParameters[0].Type.Decl
		default:
			asyncResponseType = fmt.Sprintf("%s$%s$Response", protocol.Name, val.Name)
			asyncResponseClass = asyncResponseType
		}
	}

	_, transitional := val.LookupAttribute("transitional")
	return Method{
		Method:             val,
		Ordinal:            val.Ordinal,
		OrdinalName:        fmt.Sprintf("_k%s_%s_Ordinal", protocol.Name, val.Name),
		Name:               name,
		HasRequest:         val.HasRequest,
		Request:            request,
		HasResponse:        val.HasResponse,
		Response:           response,
		AsyncResponseClass: asyncResponseClass,
		AsyncResponseType:  asyncResponseType,
		CallbackType:       fmt.Sprintf("%s%sCallback", protocol.Name, c.compileUpperCamelIdentifier(val.Name, methodContext)),
		TypeSymbol:         fmt.Sprintf("_k%s_%s_Type", protocol.Name, val.Name),
		TypeExpr:           c.typeExprForMethod(val, request, response.WireParameters, fmt.Sprintf("%s.%s", protocol.Name, val.Name)),
		Transitional:       transitional,
		Documented:         docString(val),
	}
}

func (c *compiler) compileProtocol(val fidlgen.Protocol) Protocol {
	ci := val.Name.Parse()
	r := Protocol{
		Protocol:    val,
		Name:        c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		ServiceData: c.compileUpperCamelCompoundIdentifier(ci, "Data", declarationContext),
		ProxyName:   c.compileUpperCamelCompoundIdentifier(ci, "Proxy", declarationContext),
		BindingName: c.compileUpperCamelCompoundIdentifier(ci, "Binding", declarationContext),
		ServerName:  c.compileUpperCamelCompoundIdentifier(ci, "Server", declarationContext),
		EventsName:  c.compileUpperCamelCompoundIdentifier(ci, "Events", declarationContext),
		Methods:     []Method{},
		HasEvents:   false,
		Documented:  docString(val),
	}
	if !val.OneWayUnknownInteractions() {
		r.ServerName = r.Name
	}

	for _, v := range val.Methods {
		m := c.compileMethod(v, r, val)
		r.Methods = append(r.Methods, m)
		if !v.HasRequest && v.HasResponse {
			r.HasEvents = true
		}
	}

	return r
}

func (c *compiler) compileStructMember(val fidlgen.StructMember) StructMember {
	t := c.compileType(val.Type)

	var defaultValue string
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t)
	}

	return StructMember{
		Type:         t,
		TypeSymbol:   t.typeExpr,
		Name:         c.compileLowerCamelIdentifier(val.Name, structMemberContext),
		DefaultValue: defaultValue,
		OffsetV2:     val.FieldShapeV2.Offset,
		typeExpr: fmt.Sprintf("$fidl.MemberType<%s>(type: %s, offset: %v)",
			t.Decl, t.typeExpr, val.FieldShapeV2.Offset),
		Documented: docString(val),
	}
}

func (c *compiler) compileStruct(val fidlgen.Struct) Struct {
	ci := val.Name.Parse()
	name := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
	r := Struct{
		PayloadableName: PayloadableName{name},
		Struct:          val,
		TypeSymbol:      c.typeSymbolForCompoundIdentifier(ci),
		TypeExpr: fmt.Sprintf(
			`$fidl.StructType<%s>(inlineSize: %v, structDecode: %s._structDecode)`,
			name, val.TypeShapeV2.InlineSize, name),
		Documented: docString(val),
	}

	// Early exit for empty struct case.
	if len(val.Members) == 0 {
		r.isEmptyStruct = true
		r.Members = []StructMember{
			c.compileStructMember(fidlgen.EmptyStructMember("reserved")),
		}
		r.Paddings = []StructPadding{
			{
				OffsetV2:  0,
				PaddingV2: 1,
			},
		}
		return r
	}

	var (
		isFirst           = true
		previousPaddingV2 int
	)
	for _, v := range val.Members {
		member := c.compileStructMember(v)
		if member.Type.Nullable {
			r.HasNullableField = true
		}
		r.Members = append(r.Members, member)
		if isFirst {
			isFirst = false
		} else {
			r.Paddings = append(r.Paddings, StructPadding{
				OffsetV2:  v.FieldShapeV2.Offset - previousPaddingV2,
				PaddingV2: previousPaddingV2,
			})
		}
		previousPaddingV2 = v.FieldShapeV2.Padding
	}
	r.Paddings = append(r.Paddings, StructPadding{
		OffsetV2:  val.TypeShapeV2.InlineSize - previousPaddingV2,
		PaddingV2: previousPaddingV2,
	})
	return r
}

func (c *compiler) compileTableMember(val fidlgen.TableMember) TableMember {
	t := c.compileType(*val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t)
	}

	return TableMember{
		Ordinal:      val.Ordinal,
		Index:        val.Ordinal - 1,
		Type:         t,
		Name:         c.compileLowerCamelIdentifier(val.Name, tableMemberContext),
		DefaultValue: defaultValue,
		Documented:   docString(val),
	}
}

func (c *compiler) compileTable(val fidlgen.Table) Table {
	ci := val.Name.Parse()
	r := Table{
		Table:           val,
		PayloadableName: PayloadableName{c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)},
		TypeSymbol:      c.typeSymbolForCompoundIdentifier(ci),
		Documented:      docString(val),
	}

	for _, v := range val.SortedMembersNoReserved() {
		r.Members = append(r.Members, c.compileTableMember(v))
	}

	r.TypeExpr = fmt.Sprintf(`$fidl.TableType<%s>(
  inlineSize: %v,
  members: %s,
  ctor: %s._ctor,
  resource: %t,
)`, r.Name, val.TypeShapeV2.InlineSize, formatTableMemberList(r.Members), r.Name, r.IsResourceType())
	return r
}

func (c *compiler) compileUnion(val fidlgen.Union) Union {
	var members []UnionMember
	for _, member := range val.Members {
		if member.Reserved {
			continue
		}
		memberType := c.compileType(*member.Type)
		members = append(members, UnionMember{
			Ordinal:    uint64(member.Ordinal),
			Type:       memberType,
			Name:       c.compileLowerCamelIdentifier(member.Name, unionMemberContext),
			CtorName:   c.compileUpperCamelIdentifier(member.Name, unionMemberContext),
			Tag:        c.compileLowerCamelIdentifier(member.Name, unionMemberTagContext),
			Documented: docString(member),
		})
	}

	ci := val.Name.Parse()
	name := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
	r := Union{
		Union:           val,
		PayloadableName: PayloadableName{name},
		TagName:         c.compileUpperCamelCompoundIdentifier(ci, "Tag", declarationContext),
		TypeSymbol:      c.typeSymbolForCompoundIdentifier(ci),
		OptTypeSymbol:   c.optTypeSymbolForCompoundIdentifier(ci),
		Members:         members,
		Documented:      docString(val),
	}
	r.TypeExpr = fmt.Sprintf(`$fidl.UnionType<%s>(
  members: %s,
  ctor: %s._ctor,
  flexible: %t,
  resource: %t,
)`, r.Name, formatUnionMemberList(r.Members), r.Name, r.IsFlexible(), r.IsResourceType())
	r.OptTypeExpr = fmt.Sprintf(`$fidl.NullableUnionType<%s>(
members: %s,
ctor: %s._ctor,
flexible: %t,
resource: %t,
)`, r.Name, formatUnionMemberList(r.Members), r.Name, r.IsFlexible(), r.IsResourceType())

	return r
}

// isParamableType checks to see if a type is ever used as either a message body
// type or a payload type. If it is, we will need to render the type as a list
// of parameters in some contexts (for example, for a method-calling function
// signature), which will require special treatment depending on whether or not
// the underlying FIDL layout is a `struct`.
//
// By way of example, consider the following FIDL:
//
//	MyMethod(struct{...}) -> (struct{...}) error uint32;
//	        |-----A-----|    |-----B-----| |-----C-----|
//	                         |------------D------------|
//
// Types `A` and `B` are payloads (included in the `methodTypes` set). These
// types, or their parameterized representations, are ones that users of the
// Dart bindings will interact with when sending and replying to FIDL method
// calls.
//
// Types `A` and `C` are message bodies (included in the `wireTypes` set). These
// are the types that are actually sent over the wire, and may need to be
// internally constructed from their constituent parts ("unflattened").
//
// If the `error` syntax were not used in the example above, the message body
// type name and payload type name sets for the method would be identical.
func isParamableType(name fidlgen.EncodedCompoundIdentifier, wireTypes fidlgen.EncodedCompoundIdentifierSet, methodTypes fidlgen.EncodedCompoundIdentifierSet) bool {
	if _, ok := wireTypes[name]; ok {
		return true
	}
	if _, ok := methodTypes[name]; ok {
		return true
	}
	return false
}

// Compile the language independent type definition into the Dart-specific representation.
func Compile(r fidlgen.Root) Root {
	r = r.ForBindings("dart")
	c := compiler{
		decls:          r.DeclInfo(),
		library:        r.Name.Parse(),
		typesRoot:      r,
		paramableTypes: map[fidlgen.EncodedCompoundIdentifier]Parameterizer{},
	}

	// Do a first pass of the protocols, creating a set of all names of types that
	// are used as a transactional message payloads and/or wire bodies.
	mtum := r.MethodTypeUsageMap()

	c.Root.LibraryName = fmt.Sprintf("fidl_%s", formatLibraryName(c.library))

	for _, v := range r.Consts {
		c.Root.Consts = append(c.Root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		c.Root.Enums = append(c.Root.Enums, c.compileEnum(v))
	}

	for _, v := range r.Bits {
		c.Root.Bits = append(c.Root.Bits, c.compileBits(v))
	}

	for _, v := range r.Structs {
		compiled := c.compileStruct(v)
		if k, ok := mtum[v.Name]; ok {
			c.paramableTypes[v.Name] = &compiled
			if v.IsAnonymous() && k != fidlgen.UsedOnlyAsPayload {
				// Because anonymous payload struct definitions are always exposed to
				// the user in "flattened" form as parameter lists consisting of the
				// struct's members, it is pointless to generate a Dart class for this
				// unusable Struct definition.
				continue
			}
		}
		c.Root.Structs = append(c.Root.Structs, compiled)
	}

	for _, v := range r.ExternalStructs {
		compiled := c.compileStruct(v)
		if k, ok := mtum[v.Name]; ok {
			c.paramableTypes[v.Name] = &compiled
			if v.IsAnonymous() && k != fidlgen.UsedOnlyAsPayload {
				// Because anonymous payload struct definitions are always exposed to
				// the user in "flattened" form as parameter lists consisting of the
				// struct's members, it is pointless to generate a Dart class for this
				// unusable Struct definition.
				continue
			}
		}
		c.Root.ExternalStructs = append(c.Root.ExternalStructs, compiled)
	}

	for _, v := range r.Tables {
		t := c.compileTable(v)
		if _, ok := mtum[v.Name]; ok {
			c.paramableTypes[v.Name] = &t
		}
		c.Root.Tables = append(c.Root.Tables, t)
	}

	for _, v := range r.Unions {
		u := c.compileUnion(v)
		if _, ok := mtum[v.Name]; ok {
			c.paramableTypes[v.Name] = &u
		}
		c.Root.Unions = append(c.Root.Unions, u)
	}

	// For imported table and union payloads, generate the appropriate payloadable
	// names.
	for _, v := range r.Libraries {
		for n, d := range v.Decls {
			if d.Type == fidlgen.TableDeclType || d.Type == fidlgen.UnionDeclType {
				if _, ok := mtum[n]; ok {
					ci := n.Parse()
					name := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
					pn := PayloadableName{name}
					c.paramableTypes[n] = &pn
				}
			}
		}
	}

	for _, v := range r.Protocols {
		c.Root.Protocols = append(c.Root.Protocols, c.compileProtocol(v))
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to import our own package.
			continue
		}
		library := l.Name.Parse()
		c.Root.Imports = append(c.Root.Imports, Import{
			LocalName: libraryPrefix(library),
			AsyncURL:  fmt.Sprintf("package:fidl_%s/fidl_async.dart", formatLibraryName(library)),
		})
	}

	return c.Root
}
