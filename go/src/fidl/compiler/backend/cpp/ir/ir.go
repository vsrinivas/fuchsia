// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"flag"
	"fmt"
	"io"
	"log"
	"strings"
	"text/template"
)

var legacyCallbacks = flag.Bool("cpp-legacy-callbacks", false,
	"use std::function instead of fit::function in C++ callbacks")

type Decl interface {
	ForwardDeclaration(*template.Template, io.Writer) error
	Declaration(*template.Template, io.Writer) error
	Traits(*template.Template, io.Writer) error
	Definition(*template.Template, io.Writer) error
}

type Type struct {
	Decl     string
	Dtor     string
	DeclType types.DeclType
}

type Const struct {
	Extern    bool
	Decorator string
	Type      Type
	Name      string
	Value     string
}

type Enum struct {
	Namespace string
	Type      string
	Name      string
	Members   []EnumMember
}

type EnumMember struct {
	Name  string
	Value string
}

type Union struct {
	Namespace string
	Name      string
	Members   []UnionMember
	Size      int
}

type UnionMember struct {
	Type        Type
	Name        string
	StorageName string
	TagName     string
	Offset      int
}

type Struct struct {
	Namespace string
	Name      string
	TableType string
	Members   []StructMember
	Size      int
}

type StructMember struct {
	Type         Type
	Name         string
	DefaultValue string
	Offset       int
}

type Interface struct {
	Namespace       string
	Name            string
	ServiceName     string
	ProxyName       string
	StubName        string
	EventSenderName string
	SyncName        string
	SyncProxyName   string
	Methods         []Method
}

type Method struct {
	Ordinal             types.Ordinal
	OrdinalName         string
	Name                string
	HasRequest          bool
	Request             []Parameter
	RequestSize         int
	RequestTypeName     string
	HasResponse         bool
	Response            []Parameter
	ResponseSize        int
	ResponseTypeName    string
	CallbackType        string
	ResponseHandlerType string
	ResponderType       string
}

type Parameter struct {
	Type   Type
	Name   string
	Offset int
}

type Root struct {
	PrimaryHeader   string
	Headers         []string
	Library         types.LibraryIdentifier
	LibraryReversed types.LibraryIdentifier
	Decls           []Decl
}

func (c *Const) ForwardDeclaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "ConstForwardDeclaration", c)
}

func (c *Const) Declaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "ConstDeclaration", c)
}

func (c *Const) Traits(tmpls *template.Template, wr io.Writer) error {
	return nil
}

func (c *Const) Definition(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "ConstDefinition", c)
}

func (e *Enum) ForwardDeclaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "EnumForwardDeclaration", e)
}

func (e *Enum) Declaration(tmpls *template.Template, wr io.Writer) error {
	return nil
}

func (e *Enum) Traits(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "EnumTraits", e)
}

func (e *Enum) Definition(tmpls *template.Template, wr io.Writer) error {
	return nil
}

func (u *Union) ForwardDeclaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "UnionForwardDeclaration", u)
}

func (u *Union) Declaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "UnionDeclaration", u)
}

func (u *Union) Traits(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "UnionTraits", u)
}

func (u *Union) Definition(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "UnionDefinition", u)
}

func (s *Struct) ForwardDeclaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "StructForwardDeclaration", s)
}

func (s *Struct) Declaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "StructDeclaration", s)
}

func (s *Struct) Traits(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "StructTraits", s)
}

func (s *Struct) Definition(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "StructDefinition", s)
}

func (i *Interface) ForwardDeclaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "InterfaceForwardDeclaration", i)
}

func (i *Interface) Declaration(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "InterfaceDeclaration", i)
}

func (i *Interface) Traits(tmpls *template.Template, wr io.Writer) error {
	return nil
}

func (i *Interface) Definition(tmpls *template.Template, wr io.Writer) error {
	return tmpls.ExecuteTemplate(wr, "InterfaceDefinition", i)
}

func (m *Method) CallbackWrapper() string {
	if *legacyCallbacks {
		return "std::function"
	}
	return "fit::function"
}

var reservedWords = map[string]bool{
	"alignas":          true,
	"alignof":          true,
	"and":              true,
	"and_eq":           true,
	"asm":              true,
	"atomic_cancel":    true,
	"atomic_commit":    true,
	"atomic_noexcept":  true,
	"auto":             true,
	"bitand":           true,
	"bitor":            true,
	"bool":             true,
	"break":            true,
	"case":             true,
	"catch":            true,
	"char":             true,
	"char16_t":         true,
	"char32_t":         true,
	"class":            true,
	"compl":            true,
	"concept":          true,
	"const":            true,
	"constexpr":        true,
	"const_cast":       true,
	"continue":         true,
	"co_await":         true,
	"co_return":        true,
	"co_yield":         true,
	"decltype":         true,
	"default":          true,
	"delete":           true,
	"do":               true,
	"double":           true,
	"dynamic_cast":     true,
	"else":             true,
	"enum":             true,
	"explicit":         true,
	"export":           true,
	"extern":           true,
	"false":            true,
	"float":            true,
	"for":              true,
	"friend":           true,
	"goto":             true,
	"if":               true,
	"import":           true,
	"inline":           true,
	"int":              true,
	"long":             true,
	"module":           true,
	"mutable":          true,
	"namespace":        true,
	"new":              true,
	"noexcept":         true,
	"not":              true,
	"not_eq":           true,
	"nullptr":          true,
	"operator":         true,
	"or":               true,
	"or_eq":            true,
	"private":          true,
	"protected":        true,
	"public":           true,
	"register":         true,
	"reinterpret_cast": true,
	"requires":         true,
	"return":           true,
	"short":            true,
	"signed":           true,
	"sizeof":           true,
	"static":           true,
	"static_assert":    true,
	"static_cast":      true,
	"struct":           true,
	"switch":           true,
	"synchronized":     true,
	"template":         true,
	"this":             true,
	"thread_local":     true,
	"throw":            true,
	"true":             true,
	"try":              true,
	"typedef":          true,
	"typeid":           true,
	"typename":         true,
	"union":            true,
	"unsigned":         true,
	"using":            true,
	"virtual":          true,
	"void":             true,
	"volatile":         true,
	"wchar_t":          true,
	"while":            true,
	"xor":              true,
	"xor_eq":           true,
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Status:  "zx_status_t",
	types.Int8:    "int8_t",
	types.Int16:   "int16_t",
	types.Int32:   "int32_t",
	types.Int64:   "int64_t",
	types.Uint8:   "uint8_t",
	types.Uint16:  "uint16_t",
	types.Uint32:  "uint32_t",
	types.Uint64:  "uint64_t",
	types.Float32: "float",
	types.Float64: "double",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(i types.Identifier, ext string) string {
	str := string(i) + ext
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func formatLibrary(library types.LibraryIdentifier, sep string) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(types.Identifier(strings.Join(parts, sep)), "")
}

func formatNamespace(library types.LibraryIdentifier) string {
	return "::" + formatLibrary(library, "::")
}

func formatLibraryPrefix(library types.LibraryIdentifier) string {
	return formatLibrary(library, "_")
}

func formatLibraryPath(library types.LibraryIdentifier) string {
	return formatLibrary(library, "/")
}

func formatDestructor(eci types.EncodedCompoundIdentifier) string {
	val := types.ParseCompoundIdentifier(eci)
	return fmt.Sprintf("~%s", changeIfReserved(val.Name, ""))
}

type compiler struct {
	namespace    string
	symbolPrefix string
	decls        *types.DeclMap
	library      types.LibraryIdentifier
}

func (c *compiler) isInExternalLibrary(ci types.CompoundIdentifier) bool {
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

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, ext string) string {
	val := types.ParseCompoundIdentifier(eci)
	strs := []string{}
	if c.isInExternalLibrary(val) {
		strs = append(strs, formatNamespace(val.Library))
	}
	strs = append(strs, changeIfReserved(val.Name, ext))
	return strings.Join(strs, "::")
}

func (c *compiler) compileLiteral(val types.Literal) string {
	switch val.Kind {
	case types.StringLiteral:
		return fmt.Sprintf("%q", val.Value)
	case types.NumericLiteral:
		return val.Value
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
		v := c.compileCompoundIdentifier(val.Identifier, "")
		if t != nil && t.DeclType == types.EnumDeclType {
			v = fmt.Sprintf("%s::%s", t.Decl, v)
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
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type: ", val)
	return ""
}

func (c *compiler) compileType(val types.Type) Type {
	r := Type{}
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType)
		r.Decl = fmt.Sprintf("::fidl::Array<%s, %v>", t.Decl, *val.ElementCount)
		r.Dtor = fmt.Sprintf("~Array")
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		r.Decl = fmt.Sprintf("::fidl::VectorPtr<%s>", t.Decl)
		r.Dtor = fmt.Sprintf("~VectorPtr")
	case types.StringType:
		r.Decl = "::fidl::StringPtr"
		r.Dtor = "~StringPtr"
	case types.HandleType:
		r.Decl = fmt.Sprintf("::zx::%s", val.HandleSubtype)
		r.Dtor = fmt.Sprintf("~%s", val.HandleSubtype)
	case types.RequestType:
		t := c.compileCompoundIdentifier(val.RequestSubtype, "")
		r.Decl = fmt.Sprintf("::fidl::InterfaceRequest<%s>", t)
		r.Dtor = "~InterfaceRequest"
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := c.compileCompoundIdentifier(val.Identifier, "")
		declType, ok := (*c.decls)[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.ConstDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.UnionDeclType:
			if val.Nullable {
				r.Decl = fmt.Sprintf("::std::unique_ptr<%s>", t)
				r.Dtor = fmt.Sprintf("~unique_ptr")
			} else {
				r.Decl = t
				r.Dtor = formatDestructor(val.Identifier)
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("::fidl::InterfaceHandle<%s>", t)
			r.Dtor = fmt.Sprintf("~InterfaceHandle")
		default:
			log.Fatal("Unknown declaration type: ", declType)
		}
		r.DeclType = declType
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}
	return r
}

func (c *compiler) compileConst(val types.Const) Const {
	if val.Type.Kind == types.StringType {
		return Const{
			true,
			"const",
			Type{
				Decl: "char",
			},
			c.compileCompoundIdentifier(val.Name, "[]"),
			c.compileConstant(val.Value, nil),
		}
	} else {
		t := c.compileType(val.Type)
		return Const{
			false,
			"constexpr",
			t,
			c.compileCompoundIdentifier(val.Name, ""),
			c.compileConstant(val.Value, &t),
		}
	}
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	r := Enum{
		c.namespace,
		c.compilePrimitiveSubtype(val.Type),
		c.compileCompoundIdentifier(val.Name, ""),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			changeIfReserved(v.Name, ""),
			c.compileConstant(v.Value, nil),
		})
	}
	return r
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			c.compileType(v.Type),
			changeIfReserved(v.Name, ""),
			v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		c.namespace,
		c.compileCompoundIdentifier(val.Name, ""),
		val.GetServiceName(),
		c.compileCompoundIdentifier(val.Name, "_Proxy"),
		c.compileCompoundIdentifier(val.Name, "_Stub"),
		c.compileCompoundIdentifier(val.Name, "_EventSender"),
		c.compileCompoundIdentifier(val.Name, "_Sync"),
		c.compileCompoundIdentifier(val.Name, "_SyncProxy"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := changeIfReserved(v.Name, "")
		callbackType := ""
		if v.HasResponse {
			callbackType = changeIfReserved(v.Name, "Callback")
		}
		responseTypeNameSuffix := "ResponseTable"
		if !v.HasRequest {
			responseTypeNameSuffix = "EventTable"
		}
		m := Method{
			v.Ordinal,
			fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
			name,
			v.HasRequest,
			c.compileParameterArray(v.Request),
			v.RequestSize,
			fmt.Sprintf("%s_%s%sRequestTable", c.symbolPrefix, r.Name, v.Name),
			v.HasResponse,
			c.compileParameterArray(v.Response),
			v.ResponseSize,
			fmt.Sprintf("%s_%s%s%s", c.symbolPrefix, r.Name, v.Name, responseTypeNameSuffix),
			callbackType,
			fmt.Sprintf("%s_%s_ResponseHandler", r.Name, v.Name),
			fmt.Sprintf("%s_%s_Responder", r.Name, v.Name),
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	t := c.compileType(val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t)
	}

	return StructMember{
		t,
		changeIfReserved(val.Name, ""),
		defaultValue,
		val.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	name := c.compileCompoundIdentifier(val.Name, "")
	r := Struct{
		c.namespace,
		name,
		fmt.Sprintf("%s_%sTable", c.symbolPrefix, name),
		[]StructMember{},
		val.Size,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	n := changeIfReserved(val.Name, "")
	return UnionMember{
		c.compileType(val.Type),
		n,
		changeIfReserved(val.Name, "_"),
		fmt.Sprintf("k%s", common.ToUpperCamelCase(n)),
		val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		c.namespace,
		c.compileCompoundIdentifier(val.Name, ""),
		[]UnionMember{},
		val.Size,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func Compile(r types.Root) Root {
	root := Root{}
	library := types.ParseLibraryName(r.Name)
	c := compiler{
		formatNamespace(library),
		formatLibraryPrefix(library),
		&r.Decls,
		types.ParseLibraryName(r.Name),
	}

	root.Library = library
	libraryReversed := make(types.LibraryIdentifier, len(library))
	for i, j := 0, len(library)-1; i < len(library); i, j = i+1, j-1 {
		libraryReversed[i] = library[j]
	}
	for i, identifier := range library {
		libraryReversed[len(libraryReversed)-i-1] = identifier
	}
	root.LibraryReversed = libraryReversed

	decls := map[types.EncodedCompoundIdentifier]Decl{}

	for _, v := range r.Consts {
		d := c.compileConst(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Enums {
		d := c.compileEnum(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Interfaces {
		d := c.compileInterface(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Structs {
		d := c.compileStruct(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Unions {
		d := c.compileUnion(v)
		decls[v.Name] = &d
	}

	for _, v := range r.DeclOrder {
		d := decls[v]
		if d == nil {
			log.Fatal("Unknown declaration: ", v)
		}
		root.Decls = append(root.Decls, d)
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to include our own header.
			continue
		}
		libraryIdent := types.ParseLibraryName(l.Name)
		h := fmt.Sprintf("%s/cpp/fidl.h", formatLibraryPath(libraryIdent))
		root.Headers = append(root.Headers, h)
	}

	return root
}
