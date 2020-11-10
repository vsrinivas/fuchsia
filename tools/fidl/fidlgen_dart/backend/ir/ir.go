// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"regexp"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
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
	Nullable      bool
	declType      types.DeclType
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
	types.Enum

	Name       string
	Members    []EnumMember
	TypeSymbol string
	TypeExpr   string
	Documented
}

// EnumMember represents a member of an enum declaration.
type EnumMember struct {
	types.EnumMember

	Name  string
	Value string
	Documented
}

// Bits represents a bits declaration.
type Bits struct {
	types.Bits

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
	Name          string
	TagName       string
	Members       []UnionMember
	TypeSymbol    string
	TypeExpr      string
	OptTypeSymbol string
	OptTypeExpr   string
	Documented
	types.Strictness
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
	typeExpr     string
	Documented
}

// Table represents a table declaration.
type Table struct {
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

// Interface represents an interface declaration.
type Interface struct {
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

// Method represents a method declaration within an interface declaration.
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
	Interfaces  []Interface
	Structs     []Struct
	Tables      []Table
	Unions      []Union
}

type context map[string]bool

var (
	// Name of a bits member
	bitsMemberContext = make(context)
	// Name of an enum member
	enumMemberContext = make(context)
	// Name of a struct member
	structMemberContext = make(context)
	// Name of a table member
	tableMemberContext = make(context)
	// Name of a union member
	unionMemberContext = make(context)
	// Tag of a union member
	unionMemberTagContext = make(context)
	// Name of a constant
	constantContext = make(context)
	// Name of a top-level declaration (other than a constant)
	declarationContext = make(context)
	// Name of a method
	methodContext = make(context)
	// Everywhere
)

func init() {
	var allContexts = []context{
		enumMemberContext, structMemberContext, tableMemberContext,
		unionMemberContext, unionMemberTagContext, constantContext,
		declarationContext, methodContext, bitsMemberContext,
	}

	var reservedWords = map[string][]context{
		"assert":       allContexts,
		"async":        allContexts,
		"await":        allContexts,
		"break":        allContexts,
		"bool":         {structMemberContext, tableMemberContext, enumMemberContext},
		"case":         allContexts,
		"catch":        allContexts,
		"class":        allContexts,
		"const":        allContexts,
		"continue":     allContexts,
		"default":      allContexts,
		"do":           allContexts,
		"double":       {structMemberContext, tableMemberContext},
		"dynamic":      {bitsMemberContext, enumMemberContext, methodContext, unionMemberContext, constantContext, tableMemberContext, structMemberContext},
		"else":         allContexts,
		"enum":         allContexts,
		"extends":      allContexts,
		"false":        allContexts,
		"final":        allContexts,
		"finally":      allContexts,
		"for":          allContexts,
		"hashCode":     {methodContext, bitsMemberContext, enumMemberContext, unionMemberContext, structMemberContext, tableMemberContext},
		"noSuchMethod": {methodContext, enumMemberContext, unionMemberContext, structMemberContext, tableMemberContext},
		"runtimeType":  {methodContext, enumMemberContext, unionMemberContext, structMemberContext, tableMemberContext},
		"index":        {unionMemberTagContext},
		"if":           allContexts,
		"in":           allContexts,
		"int":          {bitsMemberContext, enumMemberContext, methodContext, unionMemberContext, constantContext, tableMemberContext, structMemberContext},
		"is":           allContexts,
		"List":         {declarationContext},
		"Map":          {declarationContext},
		"new":          allContexts,
		"Never":        {declarationContext},
		"null":         allContexts,
		"Null":         {declarationContext},
		"num":          {enumMemberContext, methodContext, unionMemberContext, constantContext, tableMemberContext, structMemberContext},
		"Object":       {declarationContext},
		"override":     allContexts,
		"rethrow":      allContexts,
		"return":       allContexts,
		"String":       allContexts,
		"super":        allContexts,
		"switch":       allContexts,
		"this":         allContexts,
		"throw":        allContexts,
		"toString":     {methodContext, bitsMemberContext, enumMemberContext, structMemberContext, tableMemberContext, unionMemberContext},
		"true":         allContexts,
		"try":          allContexts,
		"values":       {unionMemberTagContext},
		"var":          allContexts,
		"void":         allContexts,
		"while":        allContexts,
		"with":         allContexts,
		"yield":        allContexts,
	}
	for word, ctxs := range reservedWords {
		for _, ctx := range ctxs {
			ctx[word] = true
		}
	}
}

func (ctx context) changeIfReserved(str string) string {
	if ctx[str] {
		return str + "$"
	}
	return str
}

var declForPrimitiveType = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Int8:    "int",
	types.Int16:   "int",
	types.Int32:   "int",
	types.Int64:   "int",
	types.Uint8:   "int",
	types.Uint16:  "int",
	types.Uint32:  "int",
	types.Uint64:  "int",
	types.Float32: "double",
	types.Float64: "double",
}

var typedDataDecl = map[types.PrimitiveSubtype]string{
	types.Int8:    "Int8List",
	types.Int16:   "Int16List",
	types.Int32:   "Int32List",
	types.Int64:   "Int64List",
	types.Uint8:   "Uint8List",
	types.Uint16:  "Uint16List",
	types.Uint32:  "Uint32List",
	types.Uint64:  "Uint64List",
	types.Float32: "Float32List",
	types.Float64: "Float64List",
}

var typeForPrimitiveSubtype = map[types.PrimitiveSubtype]string{
	types.Bool:    "BoolType",
	types.Int8:    "Int8Type",
	types.Int16:   "Int16Type",
	types.Int32:   "Int32Type",
	types.Int64:   "Int64Type",
	types.Uint8:   "Uint8Type",
	types.Uint16:  "Uint16Type",
	types.Uint32:  "Uint32Type",
	types.Uint64:  "Uint64Type",
	types.Float32: "Float32Type",
	types.Float64: "Float64Type",
}

func docStringLink(nameWithBars string) string {
	return fmt.Sprintf("[%s]", nameWithBars[1:len(nameWithBars)-1])
}

var reLink = regexp.MustCompile("\\|([^\\|]+)\\|")

// TODO(pascallouis): rethink how we depend on the types package.
type Annotated interface {
	LookupAttribute(types.Identifier) (types.Attribute, bool)
}

func docString(node Annotated) Documented {
	attribute, ok := node.LookupAttribute("Doc")
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
		return "null"
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

func formatLibraryName(library types.LibraryIdentifier) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return strings.Join(parts, "_")
}

func typeExprForPrimitiveSubtype(val types.PrimitiveSubtype) string {
	t, ok := typeForPrimitiveSubtype[val]
	if !ok {
		log.Fatal("Unknown primitive subtype: ", val)
	}
	return fmt.Sprintf("$fidl.%s()", t)
}

func libraryPrefix(library types.LibraryIdentifier) string {
	return fmt.Sprintf("lib$%s", formatLibraryName(library))
}

type compiler struct {
	decls                  types.DeclMap
	library                types.LibraryIdentifier
	typesRoot              types.Root
	requestResponsePayload map[types.EncodedCompoundIdentifier]types.Struct
}

func (c *compiler) getPayload(name types.EncodedCompoundIdentifier) types.Struct {
	val, ok := c.requestResponsePayload[name]
	if !ok {
		panic(fmt.Sprintf("Unknown request/response struct: %s", name))
	}
	return val
}

func (c *compiler) typeExprForMethod(val types.Method, request []StructMember, response []StructMember, name string) string {
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

func (c *compiler) typeSymbolForCompoundIdentifier(ident types.CompoundIdentifier) string {
	return c._typeSymbolForCompoundIdentifier(ident, "Type")
}

func (c *compiler) optTypeSymbolForCompoundIdentifier(ident types.CompoundIdentifier) string {
	return c._typeSymbolForCompoundIdentifier(ident, "OptType")
}

func (c *compiler) _typeSymbolForCompoundIdentifier(ident types.CompoundIdentifier, suffix string) string {
	t := fmt.Sprintf("k%s_%s", ident.Name, suffix)
	if c.inExternalLibrary(ident) {
		return fmt.Sprintf("%s.%s", libraryPrefix(ident.Library), t)
	}
	return t
}

func (c *compiler) compileUpperCamelIdentifier(val types.Identifier, context context) string {
	return context.changeIfReserved(common.ToUpperCamelCase(string(val)))
}

func (c *compiler) compileLowerCamelIdentifier(val types.Identifier, context context) string {
	return context.changeIfReserved(common.ToLowerCamelCase(string(val)))
}

func (c *compiler) compileCompoundIdentifier(val types.CompoundIdentifier, context context) string {
	strs := []string{}
	if c.inExternalLibrary(val) {
		strs = append(strs, libraryPrefix(val.Library))
	}
	strs = append(strs, context.changeIfReserved(string(val.Name)))
	return strings.Join(strs, ".")
}

func (c *compiler) compileUpperCamelCompoundIdentifier(val types.CompoundIdentifier, ext string, context context) string {
	str := context.changeIfReserved(common.ToUpperCamelCase(string(val.Name))) + ext
	val.Name = types.Identifier(str)
	return c.compileCompoundIdentifier(val, context)
}

func (c *compiler) compileLowerCamelCompoundIdentifier(val types.CompoundIdentifier, ext string, context context) string {
	constName := string(val.Name)
	if string(val.Member) != "" {
		constName = string(val.Member)
	}
	str := context.changeIfReserved(common.ToLowerCamelCase(string(constName))) + ext
	val.Name = types.Identifier(str)
	return c.compileCompoundIdentifier(val, context)
}

func (c *compiler) compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("%q", val.Value)
	case types.NumericLiteral:
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
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.DefaultLiteral:
		return "default"
	default:
		log.Fatal("Unknown literal kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant, t *Type) string {
	switch val.Kind {
	case types.IdentifierConstant:
		v := c.compileLowerCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier), "", constantContext)
		if t != nil && t.declType == types.EnumDeclType {
			v = fmt.Sprintf("%s.%s", t.Decl, v)
		}
		return v
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind: ", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := declForPrimitiveType[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type: ", val)
	return ""
}

func (c *compiler) maybeCompileConstant(val *types.Constant, t *Type) string {
	if val == nil {
		return "null"
	}
	return c.compileConstant(*val, t)
}

func (c *compiler) compileType(val types.Type) Type {
	r := Type{}
	r.Nullable = val.Nullable
	switch val.Kind {
	case types.ArrayType:
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
		r.typeExpr = fmt.Sprintf("$fidl.ArrayType<%s>(%s, %s)", r.Decl, elementStr, elementCountStr)
	case types.VectorType:
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
		nullableStr := fmt.Sprintf("nullable: %s", formatBool(val.Nullable))
		r.typeExpr = fmt.Sprintf("$fidl.VectorType<%s>(%s, %s, %s)",
			r.Decl, elementStr, maybeElementCountStr, nullableStr)
	case types.StringType:
		r.Decl = "String"
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		r.typeExpr = fmt.Sprintf("$fidl.StringType(maybeElementCount: %s, nullable: %s)",
			formatInt(val.ElementCount), formatBool(val.Nullable))
	case types.HandleType:
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
		r.typeExpr = fmt.Sprintf("$fidl.%sType(nullable: %s)",
			subtype, formatBool(val.Nullable))
	case types.RequestType:
		compound := types.ParseCompoundIdentifier(val.RequestSubtype)
		t := c.compileUpperCamelCompoundIdentifier(compound, "", declarationContext)
		r.Decl = fmt.Sprintf("$fidl.InterfaceRequest<%s>", t)
		if c.inExternalLibrary(compound) {
			r.SyncDecl = fmt.Sprintf("$fidl.InterfaceRequest<sync$%s>", t)
		} else {
			r.SyncDecl = fmt.Sprintf("$fidl.InterfaceRequest<$sync.%s>", t)
		}
		r.AsyncDecl = r.Decl
		r.typeExpr = fmt.Sprintf("$fidl.InterfaceRequestType<%s>(nullable: %s)",
			t, formatBool(val.Nullable))
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.SyncDecl = r.Decl
		r.AsyncDecl = r.Decl
		r.typedDataDecl = typedDataDecl[val.PrimitiveSubtype]
		r.typeExpr = typeExprForPrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		compound := types.ParseCompoundIdentifier(val.Identifier)
		t := c.compileUpperCamelCompoundIdentifier(compound, "", declarationContext)
		declType, ok := c.decls[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		r.declType = declType
		switch r.declType {
		case types.ConstDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.BitsDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.TableDeclType:
			fallthrough
		case types.UnionDeclType:
			r.Decl = t
			if c.inExternalLibrary(compound) {
				r.SyncDecl = fmt.Sprintf("sync$%s", t)

			} else {
				r.SyncDecl = fmt.Sprintf("$sync.%s", t)
			}
			r.AsyncDecl = r.SyncDecl
			if val.Nullable {
				switch r.declType {
				case types.UnionDeclType:
					r.typeExpr = c.optTypeSymbolForCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier))
				default:
					r.typeExpr = fmt.Sprintf("$fidl.PointerType<%s>(element: %s)",
						t, c.typeSymbolForCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier)))
				}
			} else {
				r.typeExpr = c.typeSymbolForCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier))
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("$fidl.InterfaceHandle<%s>", t)
			if c.inExternalLibrary(compound) {
				r.SyncDecl = fmt.Sprintf("$fidl.InterfaceHandle<sync$%s>", t)

			} else {
				r.SyncDecl = fmt.Sprintf("$fidl.InterfaceHandle<$sync.%s>", t)
			}
			r.AsyncDecl = r.Decl
			r.typeExpr = fmt.Sprintf("$fidl.InterfaceHandleType<%s>(nullable: %s)",
				t, formatBool(val.Nullable))
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
	return r
}

func (c *compiler) compileConst(val types.Const) Const {
	r := Const{
		Type:       c.compileType(val.Type),
		Name:       c.compileLowerCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.Name), "", constantContext),
		Value:      c.compileConstant(val.Value, nil),
		Documented: docString(val),
	}
	if r.Type.declType == types.EnumDeclType {
		r.Value = fmt.Sprintf("%s.%s", r.Type.Decl, r.Value)
	}
	return r
}

func (c *compiler) membersAsMapToNull(members []types.EnumMember) string {
	var values []string
	for _, member := range members {
		values = append(values, fmt.Sprintf("%s:null", c.compileConstant(member.Value, nil)))
	}
	return strings.Join(values, ",")
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	ci := types.ParseCompoundIdentifier(val.Name)
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

func (c *compiler) compileBits(val types.Bits) Bits {
	ci := types.ParseCompoundIdentifier(val.Name)
	n := c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext)
	if val.Type.Kind != types.PrimitiveType {
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

func (c *compiler) compileParameterArray(payload types.EncodedCompoundIdentifier) []StructMember {
	var parameters []StructMember
	for _, v := range c.getPayload(payload).Members {
		parameters = append(parameters, c.compileStructMember(v))
	}
	return parameters
}

func (c *compiler) compileMethodResponse(method types.Method) MethodResponse {
	var (
		resultUnion    types.Union
		resultType     types.Type
		valueStruct    *types.Struct
		valueType      types.Type
		isResult       bool
		isReponseUnion bool
		parameters     []StructMember
	)
	payload := c.getPayload(method.ResponsePayload).Members

	// Method needs to have exactly one response arg
	if !method.HasResponse || len(payload) != 1 {
		goto NotAResult
	}
	// That arg must be a non-nullable identifier
	resultType = payload[0].Type
	if resultType.Kind != types.IdentifierType || resultType.Nullable {
		goto NotAResult
	}
	// That identifier is for a union
	isReponseUnion = false
	for _, union := range c.typesRoot.Unions {
		if union.Name == resultType.Identifier {
			resultUnion = union
			isReponseUnion = true
			break
		}
	}
	if !isReponseUnion {
		goto NotAResult
	}
	// Union needs the [Result] attribute, two members
	_, isResult = resultUnion.LookupAttribute("Result")
	if !isResult || len(resultUnion.Members) != 2 {
		goto NotAResult
	}

	// Find the struct
	valueType = resultUnion.Members[0].Type
	for _, decl := range c.typesRoot.Structs {
		if decl.Name == valueType.Identifier {
			valueStruct = &decl
			break
		}
	}
	if valueStruct == nil {
		goto NotAResult
	}

	// Turn the struct into a parameter array that will be used for function arguments.
	for _, v := range valueStruct.Members {
		parameters = append(parameters, c.compileStructMember(v))
	}

	return MethodResponse{
		WireParameters:   c.compileParameterArray(method.ResponsePayload),
		MethodParameters: parameters,
		HasError:         true,
		ResultType:       c.compileUnion(resultUnion),
		ValueType:        c.compileType(resultUnion.Members[0].Type),
		ErrorType:        c.compileType(resultUnion.Members[1].Type),
	}

NotAResult:
	response := c.compileParameterArray(method.ResponsePayload)
	return MethodResponse{
		WireParameters:   response,
		MethodParameters: response,
	}
}

func (c *compiler) compileMethod(val types.Method, protocol Interface, fidlProtocol types.Interface) Method {
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

	_, transitional := val.LookupAttribute("Transitional")
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

func (c *compiler) compileInterface(val types.Interface) Interface {
	ci := types.ParseCompoundIdentifier(val.Name)
	r := Interface{
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

	if r.ServiceName == "" {
		r.ServiceName = "null"
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

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
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
		typeExpr:     fmt.Sprintf("$fidl.MemberType<%s>(%s, %s)", t.Decl, typeStr, offsetStr),
		Documented:   docString(val),
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	ci := types.ParseCompoundIdentifier(val.Name)
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
			c.compileStructMember(types.EmptyStructMember("reserved")),
		}
	}

	r.HasNullableField = hasNullableField

	r.TypeExpr = fmt.Sprintf(`$fidl.StructType<%s>(
  inlineSize: %v,
  structDecode: %s._structDecode,
)`, r.Name, val.TypeShapeV1.InlineSize, r.Name)
	return r
}

func (c *compiler) compileTableMember(val types.TableMember) TableMember {
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

func (c *compiler) compileTable(val types.Table) Table {
	ci := types.ParseCompoundIdentifier(val.Name)
	r := Table{
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
)`, r.Name, val.TypeShapeV1.InlineSize, formatTableMemberList(r.Members), r.Name)
	return r
}

func (c *compiler) compileUnion(val types.Union) Union {
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

	ci := types.ParseCompoundIdentifier(val.Name)
	r := Union{
		Name:          c.compileUpperCamelCompoundIdentifier(ci, "", declarationContext),
		TagName:       c.compileUpperCamelCompoundIdentifier(ci, "Tag", declarationContext),
		TypeSymbol:    c.typeSymbolForCompoundIdentifier(ci),
		OptTypeSymbol: c.optTypeSymbolForCompoundIdentifier(ci),
		Members:       members,
		Documented:    docString(val),
		Strictness:    val.Strictness,
	}
	r.TypeExpr = fmt.Sprintf(`$fidl.XUnionType<%s>(
  members: %s,
  ctor: %s._ctor,
  nullable: false,
  flexible: %t,
)`, r.Name, formatUnionMemberList(r.Members), r.Name, r.IsFlexible())
	r.OptTypeExpr = fmt.Sprintf(`$fidl.XUnionType<%s>(
members: %s,
ctor: %s._ctor,
nullable: true,
flexible: %t,
)`, r.Name, formatUnionMemberList(r.Members), r.Name, r.IsFlexible())

	return r
}

// Compile the language independent type definition into the Dart-specific representation.
func Compile(r types.Root) Root {
	r = r.ForBindings("dart")
	root := Root{}
	c := compiler{
		decls:                  r.DeclsWithDependencies(),
		library:                types.ParseLibraryName(r.Name),
		typesRoot:              r,
		requestResponsePayload: map[types.EncodedCompoundIdentifier]types.Struct{},
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
		if v.Anonymous {
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
		root.Interfaces = append(root.Interfaces, c.compileInterface(v))
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to import our own package.
			continue
		}
		library := types.ParseLibraryName(l.Name)
		root.Imports = append(root.Imports, Import{
			LocalName: libraryPrefix(library),
			AsyncURL:  fmt.Sprintf("package:fidl_%s/fidl_async.dart", formatLibraryName(library)),
		})
	}

	return root
}
