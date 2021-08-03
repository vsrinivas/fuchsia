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

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Documented is embedded in structs for declarations that may hold documentation.
type Documented struct {
	Doc []string
}

// Type represents a FIDL datatype.
type Type struct {
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

// Union represents a union declaration.
type Union struct {
	fidlgen.Union
	Name          string
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
	Name             string
	Members          []StructMember
	TypeSymbol       string
	TypeExpr         string
	HasNullableField bool
	Documented
}

// StructMember represents a member of a struct declaration.
type StructMember struct {
	Type         Type
	TypeSymbol   string
	Name         string
	DefaultValue string
	OffsetV1     int
	OffsetV2     int
	typeExpr     string
	Documented
}

// Table represents a table declaration.
type Table struct {
	fidlgen.Table
	Name       string
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
	fidlgen.Attributes
	Name        string
	ServiceName string
	ServiceData string
	ProxyName   string
	BindingName string
	EventsName  string
	Methods     []Method
	HasEvents   bool
	Documented
}

type MethodResponse struct {
	// WireParameters represent the parameters of the top level response struct
	// that is sent on the wire
	WireParameters []StructMember
	// MethodParameters represent the parameters that the user interacts with
	// when using generated methods. When HasError is false, this is the same as
	// WireParameters. When HasError is true, MethodParameters corresponds to the
	// fields of a successful response.
	MethodParameters []StructMember
	HasError         bool
	ResultType       Union
	ValueType        Type
	ErrorType        Type
}

// Method represents a method declaration within an protocol declaration.
type Method struct {
	Ordinal     uint64
	OrdinalName string
	Name        string
	HasRequest  bool
	Request     []StructMember
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

// Import describes another FIDL library that will be imported.
type Import struct {
	URL       string
	LocalName string
	AsyncURL  string
}

// Root holds all of the declarations for a FIDL library.
type Root struct {
	LibraryName string
	Imports     []Import
	Consts      []Const
	Enums       []Enum
	Bits        []Bits
	Protocols   []Protocol
	Structs     []Struct
	Tables      []Table
	Unions      []Union
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
	declarationContext.ReserveNames([]string{"List", "Map", "Never", "Null",
		"Object"})
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
	unionMemberContext.ReserveNames([]string{"dynamic", "hashCode", "int",
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
	var docs []string
	lines := strings.Split(attribute.Value, "\n")
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

func formatParameterList(params []StructMember) string {
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

func libraryPrefix(library fidlgen.LibraryIdentifier) string {
	return fmt.Sprintf("lib$%s", formatLibraryName(library))
}

type compiler struct {
	decls                  fidlgen.DeclInfoMap
	library                fidlgen.LibraryIdentifier
	typesRoot              fidlgen.Root
	requestResponsePayload map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct
}

func (c *compiler) getPayload(name fidlgen.EncodedCompoundIdentifier) fidlgen.Struct {
	val, ok := c.requestResponsePayload[name]
	if !ok {
		panic(fmt.Sprintf("Unknown request/response struct: %s", name))
	}
	return val
}

func (c *compiler) typeExprForMethod(val fidlgen.Method, request []StructMember, response []StructMember, name string) string {
	var (
		requestSize  = 0
		responseSize = 0
	)
	if val.RequestPayload != "" {
		payload := c.getPayload(val.RequestPayload)
		requestSize = payload.TypeShapeV1.InlineSize
	}
	if val.ResponsePayload != "" {
		payload := c.getPayload(val.ResponsePayload)
		responseSize = payload.TypeShapeV1.InlineSize
	}

	// request/response and requestInlineSize/responseInlineSize are null/0 for both empty
	// payloads and when there are no request/responses. The HasRequest and HasResponse fields
	// are used to distinguish between these two cases during codegen.
	return fmt.Sprintf(`$fidl.MethodType(
	    request: %s,
		response: %s,
		name: r"%s",
		requestInlineSize: %d,
		responseInlineSize: %d,
	  )`, formatParameterList(request), formatParameterList(response), name,
		requestSize, responseSize)
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
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("%q", val.Value)
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
	case fidlgen.TrueLiteral:
		return "true"
	case fidlgen.FalseLiteral:
		return "false"
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
		return c.compileConstantIdentifier(fidlgen.ParseCompoundIdentifier(val.Identifier), constantContext)
	case fidlgen.LiteralConstant:
		return c.compileLiteral(val.Literal)
	case fidlgen.BinaryOperator:
		if t == nil || t.declType != fidlgen.BitsDeclType {
			panic("only bits is supported")
		}
		return fmt.Sprintf("%s._(%s)", t.Decl, val.Value)
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

func (c *compiler) maybeCompileConstant(val *fidlgen.Constant, t *Type) string {
	if val == nil {
		return "null"
	}
	return c.compileConstant(*val, t)
}

func (c *compiler) compileType(val fidlgen.Type) Type {
	nullablePrefix := ""
	if val.Nullable {
		nullablePrefix = "Nullable"
	}
	r := Type{}
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
		compound := fidlgen.ParseCompoundIdentifier(val.RequestSubtype)
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
	case fidlgen.IdentifierType:
		compound := fidlgen.ParseCompoundIdentifier(val.Identifier)
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
					r.typeExpr = c.optTypeSymbolForCompoundIdentifier(fidlgen.ParseCompoundIdentifier(val.Identifier))
				default:
					r.typeExpr = fmt.Sprintf("$fidl.PointerType<%s>(element: %s)",
						t, c.typeSymbolForCompoundIdentifier(fidlgen.ParseCompoundIdentifier(val.Identifier)))
				}
			} else {
				r.typeExpr = c.typeSymbolForCompoundIdentifier(fidlgen.ParseCompoundIdentifier(val.Identifier))
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
		Name:       c.compileLowerCamelCompoundIdentifier(fidlgen.ParseCompoundIdentifier(val.Name), "", constantContext),
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
	ci := fidlgen.ParseCompoundIdentifier(val.Name)
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
	ci := fidlgen.ParseCompoundIdentifier(val.Name)
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

func (c *compiler) compileParameterArray(payload fidlgen.EncodedCompoundIdentifier) []StructMember {
	var parameters []StructMember
	for _, v := range c.getPayload(payload).Members {
		parameters = append(parameters, c.compileStructMember(v))
	}
	return parameters
}

func (c *compiler) compileMethodResponse(method fidlgen.Method) MethodResponse {
	if method.MethodResult == nil {
		response := c.compileParameterArray(method.ResponsePayload)
		return MethodResponse{
			WireParameters:   response,
			MethodParameters: response,
		}
	}

	// Turn the struct into a parameter array that will be used for function arguments.
	var parameters []StructMember
	for _, v := range method.MethodResult.ValueStruct.Members {
		parameters = append(parameters, c.compileStructMember(v))
	}

	return MethodResponse{
		WireParameters:   c.compileParameterArray(method.ResponsePayload),
		MethodParameters: parameters,
		HasError:         true,
		ResultType:       c.compileUnion(*method.MethodResult.ResultUnion),
		ValueType:        c.compileType(method.MethodResult.ValueType),
		ErrorType:        c.compileType(method.MethodResult.ErrorType),
	}
}

func (c *compiler) compileMethod(val fidlgen.Method, protocol Protocol, fidlProtocol fidlgen.Protocol) Method {
	var (
		name               = c.compileLowerCamelIdentifier(val.Name, methodContext)
		request            []StructMember
		response           MethodResponse
		asyncResponseClass string
		asyncResponseType  string
	)
	if val.HasRequest && val.RequestPayload != "" {
		request = c.compileParameterArray(val.RequestPayload)
	}
	if val.HasResponse && val.ResponsePayload != "" {
		response = c.compileMethodResponse(val)
	}

	if len(response.MethodParameters) > 1 {
		asyncResponseClass = fmt.Sprintf("%s$%s$Response", protocol.Name, val.Name)
	}
	if val.HasResponse {
		switch len(response.MethodParameters) {
		case 0:
			asyncResponseType = "void"
		case 1:
			responseType := response.MethodParameters[0].Type
			asyncResponseType = responseType.Decl
		default:
			asyncResponseType = asyncResponseClass
		}
	}

	_, transitional := val.LookupAttribute("transitional")
	return Method{
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
	ci := fidlgen.ParseCompoundIdentifier(val.Name)
	r := Protocol{
		val.Attributes,
		c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		val.GetServiceName(),
		c.compileUpperCamelCompoundIdentifier(ci, "Data", declarationContext),
		c.compileUpperCamelCompoundIdentifier(ci, "Proxy", declarationContext),
		c.compileUpperCamelCompoundIdentifier(ci, "Binding", declarationContext),
		c.compileUpperCamelCompoundIdentifier(ci, "Events", declarationContext),
		[]Method{},
		false,
		docString(val),
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

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t)
	}

	typeStr := fmt.Sprintf("type: %s", t.typeExpr)
	offsetStr := fmt.Sprintf("offset: %v", val.FieldShapeV1.Offset)
	return StructMember{
		Type:         t,
		TypeSymbol:   t.typeExpr,
		Name:         c.compileLowerCamelIdentifier(val.Name, structMemberContext),
		DefaultValue: defaultValue,
		OffsetV1:     val.FieldShapeV1.Offset,
		OffsetV2:     val.FieldShapeV2.Offset,
		typeExpr:     fmt.Sprintf("$fidl.MemberType<%s>(%s, %s)", t.Decl, typeStr, offsetStr),
		Documented:   docString(val),
	}
}

func (c *compiler) compileStruct(val fidlgen.Struct) Struct {
	ci := fidlgen.ParseCompoundIdentifier(val.Name)
	r := Struct{
		Name:             c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		Members:          []StructMember{},
		TypeSymbol:       c.typeSymbolForCompoundIdentifier(ci),
		TypeExpr:         "",
		HasNullableField: false,
		Documented:       docString(val),
	}

	var hasNullableField = false

	for _, v := range val.Members {
		var member = c.compileStructMember(v)
		if member.Type.Nullable {
			hasNullableField = true
		}
		r.Members = append(r.Members, member)
	}

	if len(r.Members) == 0 {
		r.Members = []StructMember{
			c.compileStructMember(fidlgen.EmptyStructMember("reserved")),
		}
	}

	r.HasNullableField = hasNullableField

	r.TypeExpr = fmt.Sprintf(`$fidl.StructType<%s>(
  inlineSizeV1: %v,
  inlineSizeV2: %v,
  structDecode: %s._structDecode,
)`, r.Name, val.TypeShapeV1.InlineSize, val.TypeShapeV2.InlineSize, r.Name)
	return r
}

func (c *compiler) compileTableMember(val fidlgen.TableMember) TableMember {
	t := c.compileType(val.Type)

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
	ci := fidlgen.ParseCompoundIdentifier(val.Name)
	r := Table{
		Table:      val,
		Name:       c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		TypeSymbol: c.typeSymbolForCompoundIdentifier(ci),
		Documented: docString(val),
	}

	for _, v := range val.SortedMembersNoReserved() {
		r.Members = append(r.Members, c.compileTableMember(v))
	}

	r.TypeExpr = fmt.Sprintf(`$fidl.TableType<%s>(
  inlineSize: %v,
  members: %s,
  ctor: %s._ctor,
  resource: %t,
)`, r.Name, val.TypeShapeV1.InlineSize, formatTableMemberList(r.Members), r.Name, r.IsResourceType())
	return r
}

func (c *compiler) compileUnion(val fidlgen.Union) Union {
	var members []UnionMember
	for _, member := range val.Members {
		if member.Reserved {
			continue
		}
		memberType := c.compileType(member.Type)
		members = append(members, UnionMember{
			Ordinal:    uint64(member.Ordinal),
			Type:       memberType,
			Name:       c.compileLowerCamelIdentifier(member.Name, unionMemberContext),
			CtorName:   c.compileUpperCamelIdentifier(member.Name, unionMemberContext),
			Tag:        c.compileLowerCamelIdentifier(member.Name, unionMemberTagContext),
			Documented: docString(member),
		})
	}

	ci := fidlgen.ParseCompoundIdentifier(val.Name)
	r := Union{
		Union:         val,
		Name:          c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		TagName:       c.compileUpperCamelCompoundIdentifier(ci, "Tag", declarationContext),
		TypeSymbol:    c.typeSymbolForCompoundIdentifier(ci),
		OptTypeSymbol: c.optTypeSymbolForCompoundIdentifier(ci),
		Members:       members,
		Documented:    docString(val),
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

// Compile the language independent type definition into the Dart-specific representation.
func Compile(r fidlgen.Root) Root {
	r = r.ForBindings("dart")
	root := Root{}
	c := compiler{
		decls:                  r.DeclsWithDependencies(),
		library:                fidlgen.ParseLibraryName(r.Name),
		typesRoot:              r,
		requestResponsePayload: map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{},
	}

	root.LibraryName = fmt.Sprintf("fidl_%s", formatLibraryName(c.library))

	for _, v := range r.Consts {
		root.Consts = append(root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range r.Bits {
		root.Bits = append(root.Bits, c.compileBits(v))
	}

	for _, v := range r.Structs {
		if v.IsRequestOrResponse {
			c.requestResponsePayload[v.Name] = v
		} else {
			root.Structs = append(root.Structs, c.compileStruct(v))
		}
	}

	for _, v := range r.Tables {
		root.Tables = append(root.Tables, c.compileTable(v))
	}

	for _, v := range r.Unions {
		root.Unions = append(root.Unions, c.compileUnion(v))
	}

	for _, v := range r.Protocols {
		root.Protocols = append(root.Protocols, c.compileProtocol(v))
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to import our own package.
			continue
		}
		library := fidlgen.ParseLibraryName(l.Name)
		root.Imports = append(root.Imports, Import{
			LocalName: libraryPrefix(library),
			AsyncURL:  fmt.Sprintf("package:fidl_%s/fidl_async.dart", formatLibraryName(library)),
		})
	}

	return root
}
