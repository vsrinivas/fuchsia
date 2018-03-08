// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
	"fmt"
	"io"
	"log"
	"strings"
	"text/template"
)

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
	Offset      int
}

type Struct struct {
	Namespace string
	Name      string
	CName     string
	Members   []StructMember
	Size      int
}

type StructMember struct {
	Type        Type
	Name        string
	StorageName string
	Offset      int
}

type Interface struct {
	Namespace     string
	Name          string
	ProxyName     string
	StubName      string
	SyncName      string
	SyncProxyName string
	Methods       []Method
}

type Method struct {
	Ordinal             types.Ordinal
	OrdinalName         string
	Name                string
	HasRequest          bool
	Request             []Parameter
	RequestSize         int
	HasResponse         bool
	Response            []Parameter
	ResponseSize        int
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
	PrimaryHeader string
	CHeader       string
	Namespace     string
	Decls         []Decl
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

func changeIfReserved(val types.Identifier) string {
	str := string(val)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

type compiler struct {
	namespace string
	decls     *types.DeclMap
}

func (c *compiler) compileCompoundIdentifier(val types.CompoundIdentifier) string {
	strs := []string{}
	for _, v := range val {
		strs = append(strs, changeIfReserved(v))
	}
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
		log.Fatal("Unknown literal kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compileConstant(val types.Constant) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileCompoundIdentifier(val.Identifier)
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal)
	default:
		log.Fatal("Unknown constant kind:", val.Kind)
		return ""
	}
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	log.Fatal("Unknown primitive type:", val)
	return ""
}

func (c *compiler) compileType(val types.Type) Type {
	r := Type{}
	switch val.Kind {
	case types.ArrayType:
		t := c.compileType(*val.ElementType)
		r.Decl = fmt.Sprintf("::fidl::Array<%s, %v>", t.Decl, *val.ElementCount)
		r.Dtor = fmt.Sprintf("~Array", r.Decl)
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		r.Decl = fmt.Sprintf("::fidl::VectorPtr<%s>", t.Decl)
		r.Dtor = fmt.Sprintf("~VectorPtr", r.Decl)
	case types.StringType:
		r.Decl = "::fidl::StringPtr"
		r.Dtor = "~StringPtr"
	case types.HandleType:
		r.Decl = fmt.Sprintf("::zx::%s", val.HandleSubtype)
		r.Dtor = fmt.Sprintf("~%s", val.HandleSubtype)
	case types.RequestType:
		t := c.compileCompoundIdentifier(val.RequestSubtype)
		r.Decl = fmt.Sprintf("::fidl::InterfaceRequest<%s>", t)
		r.Dtor = fmt.Sprintf("~InterfaceRequest", r.Decl)
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
	case types.IdentifierType:
		t := c.compileCompoundIdentifier(val.Identifier)
		// val.Identifier is a CompoundIdentifier, but we don't have the
		// ability to look up the declaration type for identifiers in other
		// libraries. For now, just use the first component of the identifier
		// and assume that the identifier is in this library.
		declType, ok := (*c.decls)[val.Identifier[0]]
		if !ok {
			log.Fatal("Unknown identifier:", val.Identifier)
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
				r.Dtor = fmt.Sprintf("~%s", t)
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("::fidl::InterfaceHandle<%s>", t)
			r.Dtor = fmt.Sprintf("~InterfaceHandle")
		default:
			log.Fatal("Unknown declaration type:", declType)
		}
		r.DeclType = declType
	default:
		log.Fatal("Unknown type kind:", val.Kind)
	}
	return r
}

func (c *compiler) compileConst(val types.Const) Const {
	if val.Type.Kind == types.StringType {
		r := Const{
			true,
			"const",
			Type{
				Decl: "char",
			},
			changeIfReserved(val.Name) + "[]",
			c.compileConstant(val.Value),
		}
		return r
	} else {
		r := Const{
			false,
			"constexpr",
			c.compileType(val.Type),
			changeIfReserved(val.Name),
			c.compileConstant(val.Value),
		}
		if r.Type.DeclType == types.EnumDeclType {
			r.Value = fmt.Sprintf("%s::%s", r.Type.Decl, r.Value)
		}
		return r
	}
}

func (c *compiler) compileEnum(val types.Enum) Enum {
	r := Enum{
		c.namespace,
		c.compilePrimitiveSubtype(val.Type),
		changeIfReserved(val.Name),
		[]EnumMember{},
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			changeIfReserved(v.Name),
			c.compileConstant(v.Value),
		})
	}
	return r
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			c.compileType(v.Type),
			changeIfReserved(v.Name),
			v.Offset,
		}
		r = append(r, p)
	}

	return r
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		c.namespace,
		changeIfReserved(val.Name),
		changeIfReserved(val.Name + "_Proxy"),
		changeIfReserved(val.Name + "_Stub"),
		changeIfReserved(val.Name + "_Sync"),
		changeIfReserved(val.Name + "_SyncProxy"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := changeIfReserved(v.Name)
		callbackType := ""
		if v.HasResponse {
			callbackType = changeIfReserved(v.Name + "Callback")
		}
		m := Method{
			v.Ordinal,
			fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
			name,
			v.HasRequest,
			c.compileParameterArray(v.Request),
			v.RequestSize,
			v.HasResponse,
			c.compileParameterArray(v.Response),
			v.ResponseSize,
			callbackType,
			fmt.Sprintf("%s_%s_ResponseHandler", r.Name, v.Name),
			fmt.Sprintf("%s_%s_Responder", r.Name, v.Name),
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func (c *compiler) compileStructMember(val types.StructMember) StructMember {
	return StructMember{
		c.compileType(val.Type),
		changeIfReserved(val.Name),
		changeIfReserved(val.Name + "_"),
		val.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct) Struct {
	name := changeIfReserved(val.Name)
	r := Struct{
		c.namespace,
		name,
		"::" + name,
		[]StructMember{},
		val.Size,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	return UnionMember{
		c.compileType(val.Type),
		changeIfReserved(val.Name),
		changeIfReserved(val.Name + "_"),
		val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	r := Union{
		c.namespace,
		changeIfReserved(val.Name),
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
	c := compiler{
		changeIfReserved(r.Name),
		&r.Decls,
	}

	root.Namespace = c.namespace

	decls := map[types.Identifier]Decl{}

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
		root.Decls = append(root.Decls, decls[v])
	}

	return root
}
