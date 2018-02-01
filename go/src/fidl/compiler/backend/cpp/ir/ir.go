// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"regexp"
)

type EnumValue struct {
	Name  string
	Value string
}

type Enum struct {
	Name               string
	UnderlyingDeclType string
	Values             []EnumValue
}

type UnionField struct {
	Name     string
	DeclType string
}

type Union struct {
	Name   string
	Fields []UnionField
	Size   types.SizeNum
}

type StructField struct {
	Name     string
	DeclType string
}

type Struct struct {
	Name   string
	Fields []StructField
}

type Parameter struct {
	Name string
	Type string
}

type Method struct {
	Name         string
	Ordinal      types.SizeNum
	OrdinalName  string
	HasRequest   bool
	Request      []Parameter
	HasResponse  bool
	Response     []Parameter
	CallbackType string
}

type Interface struct {
	Name      string
	ProxyName string
	StubName  string
	Methods   []Method
}

type Root struct {
	Enums      []Enum
	Interfaces []Interface
	Structs    []Struct
	Unions     []Union
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

var primitiveTypes = map[string]string{
	"int8":    "int8_t",
	"int16":   "int16_",
	"int32":   "int32_t",
	"int64":   "int64_t",
	"uint8":   "uint8_t",
	"uint16":  "uint16_t",
	"uint32":  "uint32_t",
	"uint64":  "uint64_t",
	"float32": "float",
	"float64": "double",
	"status":  "zx_status_t",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func isPrimitiveType(str string) bool {
	_, ok := primitiveTypes[str]
	return ok
}

func compilePrimitiveType(val string) string {
	t, ok := primitiveTypes[val]
	if ok {
		return t
	}
	log.Fatal("Unknown primitive type:", val)
	return ""
}

var handleRegexp = regexp.MustCompile("handle<([a-z]+)>")
var requestRegexp = regexp.MustCompile("request<(.+)>")
var stringRegexp = regexp.MustCompile("string(:.+)?")
var vectorRegexp = regexp.MustCompile("vector<(.+)>")

func compileHandleType(val string) (string, bool) {
	match := handleRegexp.FindStringSubmatch(val)
	if len(match) == 0 {
		return "", false
	}
	return "zx::" + match[1], true
}

func compileRequestType(val string) (string, bool) {
	match := requestRegexp.FindStringSubmatch(val)
	if len(match) == 0 {
		return "", false
	}
	return fmt.Sprintf("::fidl::InterfaceRequest<%s>", match[1]), true
}

func compileStringType(val string) (string, bool) {
	if stringRegexp.MatchString(val) {
		return "::fidl::StringPtr", true
	}
	return "", false
}

func compileVectorType(val string) (string, bool) {
	match := vectorRegexp.FindStringSubmatch(val)
	if len(match) == 0 {
		return "", false
	}
	t := compileType(types.TypeT(match[1]))
	return fmt.Sprintf("::fidl::VectorPtr<%s>", t), true
}

func compileType(val types.TypeT) string {
	// TODO(abarth): This would be much easier if TypeT had more structure so
	// we didn't need to parse it here.

	str := string(val)

	if isPrimitiveType(str) {
		return compilePrimitiveType(str)
	}

	if t, ok := compileHandleType(str); ok {
		return t
	}

	if t, ok := compileRequestType(str); ok {
		return t
	}

	if t, ok := compileStringType(str); ok {
		return t
	}

	if t, ok := compileVectorType(str); ok {
		return t
	}

	// TODO(abarth): How to distinguish between interfaces and structs?
	return str
}

func compileEnumType(val types.EnumType) string {
	// The enum types are a subset of the primitive types.
	return compilePrimitiveType(string(val))
}

func compileName(name types.NameT) string {
	str := string(name)
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func compileLiteral(val types.Literal) string {
	if val.IsString {
		// TODO(abarth): Escape more characters (e.g., newline).
		return fmt.Sprintf("\"%q\"", val.String)
	}
	if val.IsNumeric {
		return string(val.Numeric)
	}
	if val.IsBoolean {
		if val.Boolean {
			return "true"
		} else {
			return "false"
		}
	}
	if val.IsDefault {
		return "default"
	}
	log.Fatal("Unknown literal:", val)
	return ""
}

func compileConstant(val types.Constant) string {
	if val.IsLiteral {
		return compileLiteral(val.LiteralValue)
	}
	if val.IsIdentifier {
		return compileName(val.Identifier)
	}
	log.Fatal("Unknown constant:", val)
	return ""
}

func compileEnum(val types.EnumDeclaration) Enum {
	e := Enum{
		compileName(val.Name),
		compileEnumType(val.UnderlyingType),
		[]EnumValue{},
	}
	for _, v := range val.Values {
		e.Values = append(e.Values, EnumValue{
			compileName(v.Name),
			compileConstant(v.Value),
		})
	}
	return e
}

func compileParameterArray(val []types.Parameter) []Parameter {
	r := []Parameter{}

	for _, v := range val {
		p := Parameter{
			compileName(v.Name),
			compileType(v.Type),
		}
		r = append(r, p)
	}

	return r
}

func compileInterface(val types.InterfaceDeclaration) Interface {
	r := Interface{
		compileName(val.Name),
		compileName(val.Name + "Proxy"),
		compileName(val.Name + "Stub"),
		[]Method{},
	}

	for _, v := range val.Methods {
		name := compileName(v.Name)
		callbackType := ""
		if v.HasRequest {
			callbackType = compileName(v.Name + "Callback")
		}
		m := Method{
			name,
			v.Ordinal,
			fmt.Sprintf("k%s_%s_Ordinal", r.Name, name),
			v.HasRequest,
			compileParameterArray(v.Request),
			v.HasResponse,
			compileParameterArray(v.Response),
			callbackType,
		}
		r.Methods = append(r.Methods, m)
	}

	return r
}

func compileStruct(val types.StructDeclaration) Struct {
	return Struct{}
}

func compileUnion(val types.UnionDeclaration) Union {
	return Union{}
}

func Compile(fidlData types.Root) Root {
	root := Root{}

	for _, v := range fidlData.EnumDeclarations {
		root.Enums = append(root.Enums, compileEnum(v))
	}

	for _, v := range fidlData.InterfaceDeclarations {
		root.Interfaces = append(root.Interfaces, compileInterface(v))
	}

	for _, v := range fidlData.StructDeclarations {
		root.Structs = append(root.Structs, compileStruct(v))
	}

	for _, v := range fidlData.UnionDeclarations {
		root.Unions = append(root.Unions, compileUnion(v))
	}

	return root
}
