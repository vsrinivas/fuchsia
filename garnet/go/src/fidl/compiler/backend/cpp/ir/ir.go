// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"sort"
	"strings"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
)

// This value needs to be kept in sync with the one defined in
// zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
const llcppMaxStackAllocSize = 512

// These are used in header/impl templates to select the correct type-specific template
type bitsKind struct{}
type constKind struct{}
type enumKind struct{}
type interfaceKind struct{}
type serviceKind struct{}
type structKind struct{}
type tableKind struct{}
type xunionKind struct{}

var Kinds = struct {
	Const     constKind
	Bits      bitsKind
	Enum      enumKind
	Interface interfaceKind
	Service   serviceKind
	Struct    structKind
	Table     tableKind
	XUnion    xunionKind
}{}

type Decl interface{}

type Type struct {
	// Use Type.Decl when you want to _declare_ a class/struct, e.g. "class Foo { … }". If you need
	// to reference a class by its name (e.g. "new Foo"), use the Type.Identifier() method instead.
	// Identifier() will add a type qualifier to the class name so that the compiler will resolve
	// the class, even if any locally non-type declarations are present (e.g. "enum Foo"). Google
	// for "C++ elaborated type specifier" for more details.
	Decl   string
	LLDecl string

	Dtor   string
	LLDtor string

	DeclType types.DeclType

	IsPrimitive bool
}

func (t Type) Identifier() string {
	// TODO(FIDL-762): The logic to determine whether the type qualifier is necessary in this method
	// probably isn't correct in all cases due to the complexity of C++'s grammar rules, and could
	// be improved.

	// Don't prepend type qualifiers to fully-qualified class names, which will begin with "::"
	// (e.g. "::fidl::namespace:ClassName"): they can't be hidden by local declarations.
	if t.IsPrimitive || strings.HasPrefix(t.Decl, "::") {
		return t.Decl
	}

	return "class " + t.Decl
}

type Const struct {
	types.Attributes
	Extern    bool
	Decorator string
	Type      Type
	Name      string
	Value     string
	Kind      constKind
}

type Bits struct {
	types.Attributes
	Namespace string
	Type      string
	Name      string
	Mask      string
	MaskName  string
	Members   []BitsMember
	Kind      bitsKind
}

type BitsMember struct {
	types.Attributes
	Name  string
	Value string
}

type Enum struct {
	types.Attributes
	Namespace string
	Type      string
	Name      string
	Members   []EnumMember
	Kind      enumKind
}

type EnumMember struct {
	types.Attributes
	Name  string
	Value string
}

type XUnion struct {
	types.Attributes
	Namespace    string
	Name         string
	TableType    string
	Members      []XUnionMember
	InlineSize   int
	MaxHandles   int
	MaxOutOfLine int
	Result       *Result
	HasPointer   bool
	Kind         xunionKind
	types.Strictness
}

func (xu XUnion) ShouldReadBothOrdinals() bool {
	if len(xu.Members) == 0 {
		return false
	}
	// xunions members for any given xunion will always use the same "type"
	// (hashed/explicit) of ordinal, so we can just check the first member to
	// determine if supporting both sets are required
	return xu.Members[0].HashedOrdinal != xu.Members[0].ExplicitOrdinal
}

type XUnionMember struct {
	types.Attributes
	Ordinal         uint64
	ExplicitOrdinal uint64
	HashedOrdinal   uint64
	Type            Type
	Name            string
	StorageName     string
	TagName         string
	Offset          int
}

func (xum XUnionMember) UpperCamelCaseName() string {
	return common.ToUpperCamelCase(xum.Name)
}

type Table struct {
	types.Attributes
	Namespace      string
	Name           string
	TableType      string
	Members        []TableMember
	InlineSize     int
	BiggestOrdinal int
	MaxHandles     int
	MaxOutOfLine   int
	HasPointer     bool
	Kind           tableKind
}

type TableMember struct {
	types.Attributes
	Type              Type
	Name              string
	DefaultValue      string
	Ordinal           int
	FieldPresenceName string
	FieldDataName     string
	MethodHasName     string
	MethodClearName   string
	ValueUnionName    string
	ValueXUnionName   string
}

type Struct struct {
	types.Attributes
	Namespace     string
	Name          string
	TableType     string
	Members       []StructMember
	InlineSize    int
	MaxHandles    int
	MaxOutOfLine  int
	HasPadding    bool
	IsResultValue bool
	HasPointer    bool
	Result        *Result
	Kind          structKind
}

type StructMember struct {
	types.Attributes
	Type         Type
	Name         string
	DefaultValue string
	Offset       int
}

type Interface struct {
	types.Attributes
	Namespace           string
	Name                string
	ClassName           string
	ServiceName         string
	ProxyName           string
	StubName            string
	EventSenderName     string
	SyncName            string
	SyncProxyName       string
	RequestEncoderName  string
	RequestDecoderName  string
	ResponseEncoderName string
	ResponseDecoderName string
	Methods             []Method
	HasEvents           bool
	Kind                interfaceKind
}

type Service struct {
	types.Attributes
	Namespace   string
	Name        string
	ServiceName string
	Members     []ServiceMember
	Kind        serviceKind
}

type ServiceMember struct {
	types.Attributes
	InterfaceType string
	Name          string
	MethodName    string
}

// TODO: There are common fields between Request and Response; consider factoring them out.
type Method struct {
	types.Attributes
	types.Ordinals
	Name                  string
	NameInLowerSnakeCase  string
	HasRequest            bool
	Request               []Parameter
	RequestSize           int
	RequestTypeName       string
	RequestMaxHandles     int
	RequestMaxOutOfLine   int
	RequestPadding        bool
	RequestFlexible       bool
	RequestHasPointer     bool
	RequestContainsUnion  bool
	HasResponse           bool
	Response              []Parameter
	ResponseSize          int
	ResponseTypeName      string
	ResponseMaxHandles    int
	ResponseMaxOutOfLine  int
	ResponsePadding       bool
	ResponseFlexible      bool
	ResponseHasPointer    bool
	ResponseContainsUnion bool
	CallbackType          string
	ResponseHandlerType   string
	ResponderType         string
	Transitional          bool
	Result                *Result
	LLProps               LLProps
}

// LLContextProps contain context-dependent properties of a method specific to llcpp.
// Context is client (write request and read response) or server (read request and write response).
type LLContextProps struct {
	// Should the request be allocated on the stack, in the managed flavor.
	StackAllocRequest bool
	// Should the response be allocated on the stack, in the managed flavor.
	StackAllocResponse bool
	// Total number of bytes of stack used by storing the request and response.
	StackUse int
}

// LLProps contain properties of a method specific to llcpp
type LLProps struct {
	InterfaceName     string
	LinearizeRequest  bool
	LinearizeResponse bool
	ClientContext     LLContextProps
	ServerContext     LLContextProps
}

type Parameter struct {
	Type   Type
	Name   string
	Offset int
}

type Root struct {
	PrimaryHeader   string
	Headers         []string
	LLHeaders       []string
	LFHeaders       []string
	HandleTypes     []string
	Library         types.LibraryIdentifier
	LibraryReversed types.LibraryIdentifier
	Decls           []Decl
}

// Holds information about error results on methods
type Result struct {
	ValueMembers    []StructMember
	ResultDecl      string
	ErrorDecl       string
	ValueDecl       string
	ValueStructDecl string
	ValueTupleDecl  string
}

func (r Result) ValueArity() int {
	return len(r.ValueMembers)
}

func (m *Method) CallbackWrapper() string {
	return "fit::function"
}

var reservedWords = map[string]bool{
	"alignas":          true,
	"alignof":          true,
	"and":              true,
	"and_eq":           true,
	"asm":              true,
	"assert":           true,
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
	"NULL":             true,
	"nullptr":          true,
	"offsetof":         true,
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
	"xunion":           true,

	// names used in specific contexts e.g. union accessors
	"FidlType":        true,
	"New":             true,
	"Tag":             true,
	"Which":           true,
	"has_invalid_tag": true,
	"which":           true,
	"Unknown":         true,
	"unknown":         true,
	"IsEmpty":         true,
	"HandleEvents":    true,
	// TODO(ianloic) add: "Clone"
	// There are Clone methods on a couple of interfaces that are used
	// across layers so this will be a breaking change.
	// FIDL-461
}

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
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
	name := strings.Join(parts, sep)
	return changeIfReserved(types.Identifier(name), "")
}

func formatNamespace(library types.LibraryIdentifier, appendNamespace string) string {
	ns := "::" + formatLibrary(library, "::")
	if len(appendNamespace) > 0 {
		ns = ns + "::" + appendNamespace
	}
	return ns
}

func formatLLNamespace(library types.LibraryIdentifier, appendNamespace string) string {
	// Avoid user-defined llcpp library colliding with the llcpp namespace, by appending underscore.
	if len(library) > 0 && library[0] == "llcpp" {
		libraryRenamed := make([]types.Identifier, len(library))
		copy(libraryRenamed, library)
		libraryRenamed[0] = "llcpp_"
		library = libraryRenamed
	}
	return "::llcpp" + formatNamespace(library, appendNamespace)
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
	namespace          string
	symbolPrefix       string
	decls              types.DeclMap
	library            types.LibraryIdentifier
	handleTypes        map[types.HandleSubtype]bool
	namespaceFormatter func(types.LibraryIdentifier, string) string
	resultForStruct    map[types.EncodedCompoundIdentifier]*Result
	resultForUnion     map[types.EncodedCompoundIdentifier]*Result
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

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, ext, appendNamespace string, fullName bool) string {
	val := types.ParseCompoundIdentifier(eci)
	strs := []string{}
	if fullName || c.isInExternalLibrary(val) {
		strs = append(strs, c.namespaceFormatter(val.Library, appendNamespace))
	}
	strs = append(strs, changeIfReserved(val.Name, ext))
	if string(val.Member) != "" {
		strs = append(strs, string(val.Member))
	}
	return strings.Join(strs, "::")
}

func (c *compiler) compileTableType(eci types.EncodedCompoundIdentifier) string {
	val := types.ParseCompoundIdentifier(eci)
	if c.isInExternalLibrary(val) {
		log.Fatal("Can't create table type for external identifier: ", val)
	}

	return fmt.Sprintf("%s_%sTable", c.symbolPrefix, val.Name)
}

func (c *compiler) compileLiteral(val types.Literal, typ types.Type) string {
	switch val.Kind {
	case types.StringLiteral:
		return fmt.Sprintf("%q", val.Value)
	case types.NumericLiteral:
		if val.Value == "-9223372036854775808" || val.Value == "0x8000000000000000" {
			// C++ only supports nonnegative literals and a value this large in absolute
			// value cannot be represented as a nonnegative number in 64-bits.
			return "(-9223372036854775807ll-1)"
		}
		// TODO(FIDL-486): Once we expose resolved constants for defaults, e.g.
		// in structs, we will not need ignore hex and binary values.
		if strings.HasPrefix(val.Value, "0x") || strings.HasPrefix(val.Value, "0b") {
			return val.Value
		}

		// No special handling of floats.
		if strings.ContainsRune(val.Value, '.') {
			return val.Value
		}
		if typ.Kind == types.PrimitiveType &&
			(typ.PrimitiveSubtype == types.Float32 || typ.PrimitiveSubtype == types.Float64) {
			return val.Value
		}

		if !strings.HasPrefix(val.Value, "-") {
			return fmt.Sprintf("%su", val.Value)
		}
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

func (c *compiler) compileConstant(val types.Constant, t *Type, typ types.Type, appendNamespace string) string {
	switch val.Kind {
	case types.IdentifierConstant:
		return c.compileCompoundIdentifier(val.Identifier, "", appendNamespace, false)
	case types.LiteralConstant:
		return c.compileLiteral(val.Literal, typ)
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
		r.Decl = fmt.Sprintf("::std::array<%s, %v>", t.Decl, *val.ElementCount)
		r.LLDecl = fmt.Sprintf("::fidl::Array<%s, %v>", t.LLDecl, *val.ElementCount)
		r.Dtor = fmt.Sprintf("~array")
		r.LLDtor = fmt.Sprintf("~Array")
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		r.LLDecl = fmt.Sprintf("::fidl::VectorView<%s>", t.LLDecl)
		if val.Nullable {
			r.Decl = fmt.Sprintf("::fidl::VectorPtr<%s>", t.Decl)
			r.Dtor = fmt.Sprintf("~VectorPtr")
		} else {
			r.Decl = fmt.Sprintf("::std::vector<%s>", t.Decl)
			r.Dtor = fmt.Sprintf("~vector")
		}
	case types.StringType:
		r.LLDecl = "::fidl::StringView"
		if val.Nullable {
			r.Decl = "::fidl::StringPtr"
			r.Dtor = "~StringPtr"
		} else {
			r.Decl = "::std::string"
			r.Dtor = "~basic_string"
		}
	case types.HandleType:
		c.handleTypes[val.HandleSubtype] = true
		r.Decl = fmt.Sprintf("::zx::%s", val.HandleSubtype)
		r.LLDecl = r.Decl
		if val.HandleSubtype == "handle" {
			r.Dtor = "~object<void>"
		} else {
			r.Dtor = fmt.Sprintf("~%s", val.HandleSubtype)
		}
		r.LLDtor = r.Dtor
	case types.RequestType:
		r.Decl = fmt.Sprintf("::fidl::InterfaceRequest<%s>",
			c.compileCompoundIdentifier(val.RequestSubtype, "", "", false))
		r.LLDecl = "::zx::channel"
		r.Dtor = "~InterfaceRequest"
		r.LLDtor = "~channel"
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.LLDecl = r.Decl
		r.IsPrimitive = true
	case types.IdentifierType:
		t := c.compileCompoundIdentifier(val.Identifier, "", "", false)
		ft := c.compileCompoundIdentifier(val.Identifier, "", "", true)
		declType, ok := c.decls[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.BitsDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.ConstDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.TableDeclType:
			fallthrough
		case types.UnionDeclType:
			fallthrough
		case types.XUnionDeclType:
			if declType.IsPrimitive() {
				r.IsPrimitive = true
			}

			if val.Nullable {
				r.Decl = fmt.Sprintf("::std::unique_ptr<%s>", t)
				if declType == types.XUnionDeclType || declType == types.UnionDeclType {
					r.LLDecl = fmt.Sprintf("%s", ft)
				} else {
					r.LLDecl = fmt.Sprintf("%s*", ft)
				}
				r.Dtor = "~unique_ptr"
			} else {
				r.Decl = t
				r.LLDecl = ft
				r.Dtor = formatDestructor(val.Identifier)
				r.LLDtor = r.Dtor
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("::fidl::InterfaceHandle<class %s>", t)
			r.Dtor = fmt.Sprintf("~InterfaceHandle")
			r.LLDecl = "::zx::channel"
			r.LLDtor = "~channel"
		default:
			log.Fatal("Unknown declaration type: ", declType)
		}
		r.DeclType = declType
	default:
		log.Fatal("Unknown type kind: ", val.Kind)
	}
	return r
}

func (c *compiler) compileBits(val types.Bits, appendNamespace string) Bits {
	r := Bits{
		Attributes: val.Attributes,
		Namespace:  c.namespace,
		Type:       c.compileType(val.Type).Decl,
		Name:       c.compileCompoundIdentifier(val.Name, "", appendNamespace, false),
		Mask:       val.Mask,
		MaskName:   c.compileCompoundIdentifier(val.Name, "Mask", appendNamespace, false),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, BitsMember{
			v.Attributes,
			changeIfReserved(v.Name, ""),
			c.compileConstant(v.Value, nil, val.Type, appendNamespace),
		})
	}
	return r
}

func (c *compiler) compileConst(val types.Const, appendNamespace string) Const {
	if val.Type.Kind == types.StringType {
		return Const{
			Attributes: val.Attributes,
			Extern:     true,
			Decorator:  "const",
			Type: Type{
				Decl:   "char",
				LLDecl: "char",
			},
			Name:  c.compileCompoundIdentifier(val.Name, "[]", appendNamespace, false),
			Value: c.compileConstant(val.Value, nil, val.Type, appendNamespace),
		}
	} else {
		t := c.compileType(val.Type)
		return Const{
			Attributes: val.Attributes,
			Extern:     false,
			Decorator:  "constexpr",
			Type:       t,
			Name:       c.compileCompoundIdentifier(val.Name, "", appendNamespace, false),
			Value:      c.compileConstant(val.Value, &t, val.Type, appendNamespace),
		}
	}
}

func (c *compiler) compileEnum(val types.Enum, appendNamespace string) Enum {
	r := Enum{
		Attributes: val.Attributes,
		Namespace:  c.namespace,
		Type:       c.compilePrimitiveSubtype(val.Type),
		Name:       c.compileCompoundIdentifier(val.Name, "", appendNamespace, false),
		Members:    []EnumMember{},
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			Attributes: v.Attributes,
			Name:       changeIfReserved(v.Name, ""),
			// TODO(FIDL-324): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, nil, types.Type{
				Kind:             types.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}, appendNamespace),
		})
	}
	return r
}

func (c *compiler) compileParameterArray(val []types.Parameter) []Parameter {
	var params []Parameter = []Parameter{}
	for _, v := range val {
		params = append(params, Parameter{
			Type:   c.compileType(v.Type),
			Name:   changeIfReserved(v.Name, ""),
			Offset: v.FieldShapeV1.Offset,
		})
	}
	return params
}

// LLContext indicates where the request/response is used.
// The allocation strategies differ for client and server contexts.
type LLContext int

const (
	clientContext LLContext = iota
	serverContext LLContext = iota
)

func (m Method) NewLLContextProps(context LLContext) LLContextProps {
	stackAllocRequest := false
	stackAllocResponse := false
	if context == clientContext {
		stackAllocRequest = len(m.Request) == 0 || (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize
		stackAllocResponse = len(m.Response) == 0 || (!m.ResponseFlexible && (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize)
	} else {
		stackAllocRequest = len(m.Request) == 0 || (!m.RequestFlexible && (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize)
		stackAllocResponse = len(m.Response) == 0 || (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize
	}

	stackUse := 0
	if stackAllocRequest {
		stackUse += m.RequestSize + m.RequestMaxOutOfLine
	}
	if stackAllocResponse {
		stackUse += m.ResponseSize + m.ResponseMaxOutOfLine
	}
	return LLContextProps{
		StackAllocRequest:  stackAllocRequest,
		StackAllocResponse: stackAllocResponse,
		StackUse:           stackUse,
	}
}

func (m Method) NewLLProps(r Interface, reqTypeShape types.TypeShape, respTypeShape types.TypeShape) LLProps {
	return LLProps{
		InterfaceName:     r.Name,
		LinearizeRequest:  len(m.Request) > 0 && reqTypeShape.Depth > 0,
		LinearizeResponse: len(m.Response) > 0 && respTypeShape.Depth > 0,
		ClientContext:     m.NewLLContextProps(clientContext),
		ServerContext:     m.NewLLContextProps(serverContext),
	}
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		Attributes:          val.Attributes,
		Namespace:           c.namespace,
		Name:                c.compileCompoundIdentifier(val.Name, "", "", false),
		ClassName:           c.compileCompoundIdentifier(val.Name, "_clazz", "", false),
		ServiceName:         val.GetServiceName(),
		ProxyName:           c.compileCompoundIdentifier(val.Name, "_Proxy", "", false),
		StubName:            c.compileCompoundIdentifier(val.Name, "_Stub", "", false),
		EventSenderName:     c.compileCompoundIdentifier(val.Name, "_EventSender", "", false),
		SyncName:            c.compileCompoundIdentifier(val.Name, "_Sync", "", false),
		SyncProxyName:       c.compileCompoundIdentifier(val.Name, "_SyncProxy", "", false),
		RequestEncoderName:  c.compileCompoundIdentifier(val.Name, "_RequestEncoder", "", false),
		RequestDecoderName:  c.compileCompoundIdentifier(val.Name, "_RequestDecoder", "", false),
		ResponseEncoderName: c.compileCompoundIdentifier(val.Name, "_ResponseEncoder", "", false),
		ResponseDecoderName: c.compileCompoundIdentifier(val.Name, "_ResponseDecoder", "", false),
	}

	hasEvents := false
	for _, v := range val.Methods {
		name := changeIfReserved(v.Name, "")
		callbackType := ""
		if v.HasResponse {
			callbackType = changeIfReserved(v.Name, "Callback")
		}
		responseTypeNameSuffix := "ResponseTable"
		if !v.HasRequest {
			responseTypeNameSuffix = "EventTable"
			hasEvents = true
		}

		var result *Result
		if v.HasResponse && len(v.Response) == 1 && v.Response[0].Name == "result" {
			result = c.resultForUnion[v.Response[0].Type.Identifier]
		}

		m := Method{
			Attributes: v.Attributes,
			Ordinals: types.NewOrdinalsStep7(
				v,
				fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
				fmt.Sprintf("k%s_%s_GenOrdinal", r.Name, v.Name),
			),
			Name:                  name,
			NameInLowerSnakeCase:  common.ToSnakeCase(name),
			HasRequest:            v.HasRequest,
			Request:               c.compileParameterArray(v.Request),
			RequestSize:           v.RequestTypeShapeV1.InlineSize,
			RequestTypeName:       fmt.Sprintf("v1_%s_%s%sRequestTable", c.symbolPrefix, r.Name, v.Name),
			RequestMaxHandles:     v.RequestTypeShapeV1.MaxHandles,
			RequestMaxOutOfLine:   v.RequestTypeShapeV1.MaxOutOfLine,
			RequestPadding:        v.RequestTypeShapeV1.HasPadding,
			RequestFlexible:       v.RequestTypeShapeV1.HasFlexibleEnvelope,
			RequestHasPointer:     v.RequestTypeShapeV1.Depth > 0,
			RequestContainsUnion:  v.RequestTypeShapeV1.ContainsUnion,
			HasResponse:           v.HasResponse,
			Response:              c.compileParameterArray(v.Response),
			ResponseSize:          v.ResponseTypeShapeV1.InlineSize,
			ResponseTypeName:      fmt.Sprintf("v1_%s_%s%s%s", c.symbolPrefix, r.Name, v.Name, responseTypeNameSuffix),
			ResponseMaxHandles:    v.ResponseTypeShapeV1.MaxHandles,
			ResponseMaxOutOfLine:  v.ResponseTypeShapeV1.MaxOutOfLine,
			ResponsePadding:       v.ResponseTypeShapeV1.HasPadding,
			ResponseFlexible:      v.ResponseTypeShapeV1.HasFlexibleEnvelope,
			ResponseHasPointer:    v.ResponseTypeShapeV1.Depth > 0,
			ResponseContainsUnion: v.ResponseTypeShapeV1.ContainsUnion,
			CallbackType:          callbackType,
			ResponseHandlerType:   fmt.Sprintf("%s_%s_ResponseHandler", r.Name, v.Name),
			ResponderType:         fmt.Sprintf("%s_%s_Responder", r.Name, v.Name),
			Transitional:          v.IsTransitional(),
			Result:                result,
		}

		m.LLProps = m.NewLLProps(r, v.RequestTypeShapeV1, v.ResponseTypeShapeV1)
		r.Methods = append(r.Methods, m)
	}
	r.HasEvents = hasEvents

	return r
}

func (c *compiler) compileService(val types.Service) Service {
	s := Service{
		Attributes:  val.Attributes,
		Namespace:   c.namespace,
		Name:        c.compileCompoundIdentifier(val.Name, "", "", false),
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		s.Members = append(s.Members, c.compileServiceMember(v))
	}
	return s
}

func (c *compiler) compileServiceMember(val types.ServiceMember) ServiceMember {
	return ServiceMember{
		Attributes:    val.Attributes,
		InterfaceType: c.compileCompoundIdentifier(val.Type.Identifier, "", "", false),
		Name:          string(val.Name),
		MethodName:    changeIfReserved(val.Name, ""),
	}
}

func (c *compiler) compileStructMember(val types.StructMember, appendNamespace string) StructMember {
	t := c.compileType(val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type, appendNamespace)
	}

	return StructMember{
		Attributes:   val.Attributes,
		Type:         t,
		Name:         changeIfReserved(val.Name, ""),
		DefaultValue: defaultValue,
		Offset:       val.FieldShapeV1.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct, appendNamespace string) Struct {
	name := c.compileCompoundIdentifier(val.Name, "", appendNamespace, false)
	tableType := c.compileTableType(val.Name)
	r := Struct{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         name,
		TableType:    "v1_" + tableType,
		Members:      []StructMember{},
		InlineSize:   val.TypeShapeV1.InlineSize,
		MaxHandles:   val.TypeShapeV1.MaxHandles,
		MaxOutOfLine: val.TypeShapeV1.MaxOutOfLine,
		HasPadding:   val.TypeShapeV1.HasPadding,
		HasPointer:   val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v, appendNamespace))
	}

	result := c.resultForStruct[val.Name]
	if result != nil {
		(*result).ValueMembers = r.Members
		memberTypeDecls := []string{}
		for _, m := range r.Members {
			memberTypeDecls = append(memberTypeDecls, m.Type.Decl)
		}
		(*result).ValueTupleDecl = fmt.Sprintf("std::tuple<%s>", strings.Join(memberTypeDecls, ", "))

		if len(r.Members) == 0 {
			(*result).ValueDecl = "void"
		} else if len(r.Members) == 1 {
			(*result).ValueDecl = r.Members[0].Type.Decl
		} else {

			(*result).ValueDecl = (*result).ValueTupleDecl
		}

		r.IsResultValue = true
		r.Result = result
	}

	if len(r.Members) == 0 {
		r.Members = []StructMember{
			c.compileStructMember(types.EmptyStructMember("__reserved"), appendNamespace),
		}
	}

	return r
}

func (c *compiler) compileTableMember(val types.TableMember, appendNamespace string) TableMember {
	t := c.compileType(val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type, appendNamespace)
	}

	return TableMember{
		Attributes:        val.Attributes,
		Type:              t,
		Name:              changeIfReserved(val.Name, ""),
		DefaultValue:      defaultValue,
		Ordinal:           val.Ordinal,
		FieldPresenceName: fmt.Sprintf("has_%s_", val.Name),
		FieldDataName:     fmt.Sprintf("%s_value_", val.Name),
		MethodHasName:     fmt.Sprintf("has_%s", val.Name),
		MethodClearName:   fmt.Sprintf("clear_%s", val.Name),
		ValueUnionName:    fmt.Sprintf("ValueUnion_%s", val.Name),
	}
}

func (c *compiler) compileTable(val types.Table, appendNamespace string) Table {
	name := c.compileCompoundIdentifier(val.Name, "", appendNamespace, false)
	tableType := c.compileTableType(val.Name)
	r := Table{
		Attributes:     val.Attributes,
		Namespace:      c.namespace,
		Name:           name,
		TableType:      "v1_" + tableType,
		Members:        nil,
		InlineSize:     val.TypeShapeV1.InlineSize,
		BiggestOrdinal: 0,
		MaxHandles:     val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:   val.TypeShapeV1.MaxOutOfLine,
		HasPointer:     val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.SortedMembersNoReserved() {
		m := c.compileTableMember(v, appendNamespace)
		if m.Ordinal > r.BiggestOrdinal {
			r.BiggestOrdinal = m.Ordinal
		}
		r.Members = append(r.Members, m)
	}

	return r
}

func (c *compiler) compileXUnionMember(val types.XUnionMember) XUnionMember {
	n := changeIfReserved(val.Name, "")
	return XUnionMember{
		Attributes:      val.Attributes,
		Ordinal:         uint64(val.Ordinal),
		ExplicitOrdinal: uint64(val.ExplicitOrdinal),
		HashedOrdinal:   uint64(val.HashedOrdinal),
		Type:            c.compileType(val.Type),
		Name:            n,
		StorageName:     changeIfReserved(val.Name, "_"),
		TagName:         fmt.Sprintf("k%s", common.ToUpperCamelCase(n)),
		Offset:          val.Offset,
	}
}

func (c *compiler) compileXUnion(val types.XUnion) XUnion {
	name := c.compileCompoundIdentifier(val.Name, "", "", false)
	tableType := c.compileTableType(val.Name)
	r := XUnion{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         name,
		TableType:    "v1_" + tableType,
		InlineSize:   val.TypeShapeV1.InlineSize,
		MaxHandles:   val.TypeShapeV1.MaxHandles,
		MaxOutOfLine: val.TypeShapeV1.MaxOutOfLine,
		Strictness:   val.Strictness,
		HasPointer:   val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileXUnionMember(v))
	}

	if val.Attributes.HasAttribute("Result") {
		if len(r.Members) != 2 {
			log.Fatal("A Result union must have two members: ", val.Name)
		}
		if val.Members[0].Type.Kind != types.IdentifierType {
			log.Fatal("Value member of result union must be an identifier", val.Name)
		}
		valueStructDeclType, ok := c.decls[val.Members[0].Type.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Members[0].Type.Identifier)
		}
		if valueStructDeclType != "struct" {
			log.Fatal("First member of result union not a struct: ", val.Name)
		}
		result := Result{
			ResultDecl:      r.Name,
			ValueStructDecl: r.Members[0].Type.Decl,
			ErrorDecl:       r.Members[1].Type.Decl,
		}
		c.resultForStruct[val.Members[0].Type.Identifier] = &result
		c.resultForUnion[val.Name] = &result
		r.Result = &result
	}

	return r
}

func compile(r types.Root, namespaceFormatter func(types.LibraryIdentifier, string) string) Root {
	root := Root{}
	library := make(types.LibraryIdentifier, 0)
	raw_library := make(types.LibraryIdentifier, 0)
	for _, identifier := range types.ParseLibraryName(r.Name) {
		safe_name := changeIfReserved(identifier, "")
		library = append(library, types.Identifier(safe_name))
		raw_library = append(raw_library, identifier)
	}
	c := compiler{
		namespaceFormatter(library, ""),
		formatLibraryPrefix(raw_library),
		r.DeclsWithDependencies(),
		types.ParseLibraryName(r.Name),
		make(map[types.HandleSubtype]bool),
		namespaceFormatter,
		make(map[types.EncodedCompoundIdentifier]*Result),
		make(map[types.EncodedCompoundIdentifier]*Result),
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
	for _, v := range r.Bits {
		d := c.compileBits(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Consts {
		d := c.compileConst(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Enums {
		d := c.compileEnum(v, "")
		decls[v.Name] = &d
	}

	// Note: for Result calculation unions must be compiled before structs.
	for _, v := range r.Unions {
		var d Decl
		vConverted := types.ConvertUnionToXUnion(v)
		d = c.compileXUnion(vConverted)
		decls[v.Name] = d
	}

	for _, v := range r.XUnions {
		d := c.compileXUnion(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Structs {
		// TODO(7704) remove once anonymous structs are supported
		if v.Anonymous {
			continue
		}
		d := c.compileStruct(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Tables {
		d := c.compileTable(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Interfaces {
		d := c.compileInterface(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Services {
		d := c.compileService(v)
		decls[v.Name] = &d
	}

	for _, v := range r.DeclOrder {
		// We process only a subset of declarations mentioned in the declaration
		// order, ignore those we do not support.
		if d, known := decls[v]; known {
			root.Decls = append(root.Decls, d)
		}
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to include our own header.
			continue
		}
		libraryIdent := types.ParseLibraryName(l.Name)
		root.Headers = append(root.Headers, fmt.Sprintf("%s/cpp/fidl.h", formatLibraryPath(libraryIdent)))
		root.LFHeaders = append(root.LFHeaders, fmt.Sprintf("%s/cpp/libfuzzer.h", formatLibraryPath(libraryIdent)))
		root.LLHeaders = append(root.LLHeaders, fmt.Sprintf("%s/llcpp/fidl.h", formatLibraryPath(libraryIdent)))
	}

	// zx::channel is always referenced by the interfaces in llcpp bindings API
	if len(r.Interfaces) > 0 {
		c.handleTypes["channel"] = true
	}

	// find all unique handle types referenced by the library
	var handleTypes []string
	for k := range c.handleTypes {
		handleTypes = append(handleTypes, string(k))
	}
	sort.Sort(sort.StringSlice(handleTypes))
	root.HandleTypes = handleTypes

	return root
}

func Compile(r types.Root) Root {
	return compile(r, formatNamespace)
}

func CompileLL(r types.Root) Root {
	return compile(r, formatLLNamespace)
}
