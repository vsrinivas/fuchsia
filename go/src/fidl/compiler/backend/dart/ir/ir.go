// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strconv"
	"strings"
)

type Type struct {
	Decl          string
	Nullable      bool
	declType      types.DeclType
	typedDataDecl string
	typeExpr      string
}

type Const struct {
	Type  Type
	Name  string
	Value string
}

type Enum struct {
	Name       string
	Members    []EnumMember
	TypeSymbol string
	TypeExpr   string
}

type EnumMember struct {
	Name  string
	Value string
}

type Union struct {
	Name       string
	TagName    string
	Members    []UnionMember
	TypeSymbol string
	TypeExpr   string
}

type UnionMember struct {
	Type     Type
	Name     string
	Offset   int
	typeExpr string
}

type Struct struct {
	Name       string
	Members    []StructMember
	TypeSymbol string
	TypeExpr   string
}

type StructMember struct {
	Type         Type
	Name         string
	DefaultValue string
	Offset       int
	typeExpr     string
}

type Interface struct {
	Name        string
	ServiceName string
	ProxyName   string
	BindingName string
	Methods     []Method
}

type Method struct {
	Ordinal      types.Ordinal
	OrdinalName  string
	Name         string
	HasRequest   bool
	Request      []Parameter
	RequestSize  int
	HasResponse  bool
	Response     []Parameter
	ResponseSize int
	TypeSymbol   string
	TypeExpr     string
}

type Parameter struct {
	Type     Type
	Name     string
	Offset   int
	typeExpr string
}

type Root struct {
	Consts     []Const
	Enums      []Enum
	Interfaces []Interface
	Structs    []Struct
	Unions     []Union
}

var reservedWords = map[string]bool{
	"abstract":   true,
	"as":         true,
	"assert":     true,
	"async":      true,
	"await":      true,
	"break":      true,
	"case":       true,
	"catch":      true,
	"class":      true,
	"const":      true,
	"continue":   true,
	"covariant":  true,
	"default":    true,
	"deferred":   true,
	"do":         true,
	"dynamic":    true,
	"else":       true,
	"enum":       true,
	"export":     true,
	"extends":    true,
	"external":   true,
	"factory":    true,
	"false":      true,
	"final":      true,
	"finally":    true,
	"for":        true,
	"get":        true,
	"if":         true,
	"implements": true,
	"import":     true,
	"in":         true,
	"is":         true,
	"library":    true,
	"new":        true,
	"null":       true,
	"operator":   true,
	"part":       true,
	"rethrow":    true,
	"return":     true,
	"set":        true,
	"static":     true,
	"super":      true,
	"switch":     true,
	"this":       true,
	"throw":      true,
	"true":       true,
	"try":        true,
	"typedef":    true,
	"var":        true,
	"void":       true,
	"while":      true,
	"with":       true,
	"yield":      true,
}

var declForPrimitiveType = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "int",
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
	types.Bool:    "Uint8List",
	types.Status:  "Int32List",
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
	types.Status:  "StatusType",
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
		return "null"
	}

	lines := []string{}

	for _, p := range params {
		lines = append(lines, fmt.Sprintf("      %s,\n", p.typeExpr))
	}

	return fmt.Sprintf("const <$fidl.MemberType>[\n%s    ]", strings.Join(lines, ""))
}

func formatStructMemberList(members []StructMember) string {
	if len(members) == 0 {
		return "const <$fidl.MemberType>[]"
	}

	lines := []string{}

	for _, v := range members {
		lines = append(lines, fmt.Sprintf("    %s,\n", v.typeExpr))
	}

	return fmt.Sprintf("const <$fidl.MemberType>[\n%s  ]", strings.Join(lines, ""))
}

func formatUnionMemberList(members []UnionMember) string {
	if len(members) == 0 {
		return "const <$fidl.MemberType>[]"
	}

	lines := []string{}

	for _, v := range members {
		lines = append(lines, fmt.Sprintf("    %s,\n", v.typeExpr))
	}

	return fmt.Sprintf("const <$fidl.MemberType>[\n%s  ]", strings.Join(lines, ""))
}

func typeExprForMethod(request []Parameter, response []Parameter) string {
	return fmt.Sprintf(`const $fidl.MethodType(
    request: %s,
    response: %s,
  )`, formatParameterList(request), formatParameterList(response))
}

func typeExprForPrimitiveSubtype(val types.PrimitiveSubtype) string {
	t, ok := typeForPrimitiveSubtype[val]
	if !ok {
		log.Fatal("Unknown primitive subtype:", val)
	}
	return fmt.Sprintf("const $fidl.%s()", t)
}

func typeSymbolForCompoundIdentifier(ident types.CompoundIdentifier) string {
	// TODO(abarth): Figure out how to reference type symbols in other libraries.
	return fmt.Sprintf("k%s_Type", ident.Name)
}

func isReservedWord(str types.Identifier) bool {
	_, ok := reservedWords[string(str)]
	return ok
}

func changeIfReserved(val types.Identifier) string {
	str := string(val)
	if isReservedWord(val) {
		return str + "_"
	}
	return str
}

type compiler struct {
	decls *types.DeclMap
}

func (c *compiler) compileUpperCamelIdentifier(val types.Identifier) string {
	return common.ToUpperCamelCase(changeIfReserved(val))
}

func (c *compiler) compileLowerCamelIdentifier(val types.Identifier) string {
	return common.ToLowerCamelCase(changeIfReserved(val))
}

func (c *compiler) compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	if val.Library != "" {
		strs = append(strs, changeIfReserved(val.Library))
	}
	for _, v := range val.NestedDecls {
		strs = append(strs, c.compileUpperCamelIdentifier(v))
	}
	strs = append(strs, changeIfReserved(val.Name))
	return strings.Join(strs, ".")
}

func (c *compiler) compileUpperCamelCompoundIdentifier(val types.CompoundIdentifier, ext string) string {
	str := common.ToUpperCamelCase(changeIfReserved(val.Name)) + ext
	val.Name = types.Identifier(str)
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileLowerCamelCompoundIdentifier(val types.CompoundIdentifier, ext string) string {
	str := common.ToLowerCamelCase(changeIfReserved(val.Name)) + ext
	val.Name = types.Identifier(str)
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("%q", val.Value)
	case types.NumericLiteral:
		// TODO(abarth): Values larger than max int64 need to be encoded in hex.
		return val.Value
	case types.TrueLiteral:
		return "true"
	case types.FalseLiteral:
		return "false"
	case types.DefaultLiteral:
		return "default"
	default:
		log.Fatal("Unknown literal kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileLowerCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier), "")
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := declForPrimitiveType[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type:", val)
	return ""
}

func (c *compiler) maybeCompileConstant(val *types.Constant) string {
	if val == nil {
		return "null"
	}
	return c.compileConstant(*val)
}

func (c *compiler) compileType(val types.Type) Type {
	r := Type{}
	r.Nullable = val.Nullable
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
		}
		elementStr := fmt.Sprintf("element: %s", t.typeExpr)
		elementCountStr := fmt.Sprintf("elementCount: %s", formatInt(val.ElementCount))
		r.typeExpr = fmt.Sprintf("const $fidl.ArrayType<%s>(%s, %s)", r.Decl, elementStr, elementCountStr)
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		if len(t.typedDataDecl) > 0 {
			r.Decl = t.typedDataDecl
		} else {
			r.Decl = fmt.Sprintf("List<%s>", t.Decl)
		}
		elementStr := fmt.Sprintf("element: %s", t.typeExpr)
		maybeElementCountStr := fmt.Sprintf("maybeElementCount: %s", formatInt(val.ElementCount))
		nullableStr := fmt.Sprintf("nullable: %s", formatBool(val.Nullable))
		r.typeExpr = fmt.Sprintf("const $fidl.VectorType<%s>(%s, %s, %s)",
			r.Decl, elementStr, maybeElementCountStr, nullableStr)
	case types.StringType:
		r.Decl = "String"
		r.typeExpr = fmt.Sprintf("const $fidl.StringType(maybeElementCount: %s, nullable: %s)",
			formatInt(val.ElementCount), formatBool(val.Nullable))
	case types.HandleType:
		r.Decl = "Handle"
		r.typeExpr = fmt.Sprintf("const $fidl.HandleType(nullable: %s)",
			formatBool(val.Nullable))
	case types.RequestType:
		t := c.compileUpperCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.RequestSubtype), "")
		r.Decl = fmt.Sprintf("$fidl.InterfaceRequest<%s>", t)
		r.typeExpr = fmt.Sprintf("const $fidl.InterfaceRequestType<%s>(nullable: %s)",
			t, formatBool(val.Nullable))
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.typedDataDecl = typedDataDecl[val.PrimitiveSubtype]
		r.typeExpr = typeExprForPrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := c.compileUpperCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier), "")
		declType, ok := (*c.decls)[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier:", val.Identifier)
		}
		r.declType = declType
		switch r.declType {
		case types.ConstDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.UnionDeclType:
			r.Decl = t
			r.typeExpr = typeSymbolForCompoundIdentifier(types.ParseCompoundIdentifier(val.Identifier))
			if val.Nullable {
				r.typeExpr = fmt.Sprintf("const $fidl.PointerType<%s>(element: %s)",
					t, r.typeExpr)
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("$fidl.InterfaceHandle<%s>", t)
			r.typeExpr = fmt.Sprintf("const $fidl.InterfaceHandleType<%s>(nullable: %s)",
				t, formatBool(val.Nullable))
		default:
			log.Fatal("Unknown declaration type:", r.declType)
		}
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}
	return r
}

func (c *compiler) compileConst(val types.Const) Const {
	r := Const{
		c.compileType(val.Type),
		c.compileLowerCamelCompoundIdentifier(types.ParseCompoundIdentifier(val.Name), ""),
		c.compileConstant(val.Value),
	}
	if r.Type.declType == types.EnumDeclType {
		r.Value = fmt.Sprintf("%s.%s", r.Type.Decl, r.Value)
	}
	return r
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	ci := types.ParseCompoundIdentifier(val.Name)
	n := c.compileUpperCamelCompoundIdentifier(ci, "")
	e := Enum{
		n,
		[]EnumMember{},
		typeSymbolForCompoundIdentifier(ci),
		fmt.Sprintf("const $fidl.EnumType<%s>(type: %s, ctor: %s._ctor)", n, typeExprForPrimitiveSubtype(val.Type), n),
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			c.compileLowerCamelIdentifier(v.Name),
			c.compileConstant(v.Value),
		})
	}
	return e
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		t := c.compileType(v.Type)
		typeStr := fmt.Sprintf("type: %s", t.typeExpr)
		offsetStr := fmt.Sprintf("offset: %v", v.Offset)
		p := Parameter{
			t,
			c.compileLowerCamelIdentifier(v.Name),
			v.Offset,
			fmt.Sprintf("const $fidl.MemberType<%s>(%s, %s)", t.Decl, typeStr, offsetStr),
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	ci := types.ParseCompoundIdentifier(val.Name)
	r := Interface{
		c.compileUpperCamelCompoundIdentifier(ci, ""),
		val.GetAttribute("ServiceName"),
		c.compileUpperCamelCompoundIdentifier(ci, "Proxy"),
		c.compileUpperCamelCompoundIdentifier(ci, "Binding"),
		[]Method{},
	}

	if r.ServiceName == "" {
		r.ServiceName = "null"
	}

	for _, v := range val.Methods {
		name := c.compileLowerCamelIdentifier(v.Name)
		request := c.compileParameterArray(v.Request)
		response := c.compileParameterArray(v.Response)
		m := Method{
			v.Ordinal,
			fmt.Sprintf("_k%s_%s_Ordinal", r.Name, v.Name),
			name,
			v.HasRequest,
			request,
			v.RequestSize,
			v.HasResponse,
			response,
			v.ResponseSize,
			fmt.Sprintf("_k%s_%s_Type", r.Name, v.Name),
			typeExprForMethod(request, response),
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue)
	}

	t := c.compileType(val.Type)
	typeStr := fmt.Sprintf("type: %s", t.typeExpr)
	offsetStr := fmt.Sprintf("offset: %v", val.Offset)
	return StructMember{
		t,
		c.compileLowerCamelIdentifier(val.Name),
		defaultValue,
		val.Offset,
		fmt.Sprintf("const $fidl.MemberType<%s>(%s, %s)", t.Decl, typeStr, offsetStr),
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	ci := types.ParseCompoundIdentifier(val.Name)
	r := Struct{
		c.compileUpperCamelCompoundIdentifier(ci, ""),
		[]StructMember{},
		typeSymbolForCompoundIdentifier(ci),
		"",
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	r.TypeExpr = fmt.Sprintf(`const $fidl.StructType<%s>(
  encodedSize: %v,
  members: %s,
  ctor: %s._ctor,
)`, r.Name, val.Size, formatStructMemberList(r.Members), r.Name)
	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	t := c.compileType(val.Type)
	typeStr := fmt.Sprintf("type: %s", t.typeExpr)
	offsetStr := fmt.Sprintf("offset: %v", val.Offset)
	return UnionMember{
		t,
		c.compileLowerCamelIdentifier(val.Name),
		val.Offset,
		fmt.Sprintf("const $fidl.MemberType<%s>(%s, %s)", t.Decl, typeStr, offsetStr),
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	ci := types.ParseCompoundIdentifier(val.Name)
	r := Union{
		c.compileUpperCamelCompoundIdentifier(ci, ""),
		c.compileUpperCamelCompoundIdentifier(ci, "Tag"),
		[]UnionMember{},
		typeSymbolForCompoundIdentifier(ci),
		"",
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	r.TypeExpr = fmt.Sprintf(`const $fidl.UnionType<%s>(
  encodedSize: %v,
  members: %s,
  ctor: %s._ctor,
)`, r.Name, val.Size, formatUnionMemberList(r.Members), r.Name)
	return r
}

func Compile(r types.Root) Root {
	root := Root{}
	c := compiler{&r.Decls}

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

	for _, v := range r.Unions {
		root.Unions = append(root.Unions, c.compileUnion(v))
	}

	return root
}
