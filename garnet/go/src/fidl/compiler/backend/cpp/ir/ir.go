// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"log"
	"math"
	"sort"
	"strings"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
)

const llcppMaxStackAllocSize = 512

// These are used in header/impl templates to select the correct type-specific template
type bitsKind struct{}
type constKind struct{}
type enumKind struct{}
type interfaceKind struct{}
type structKind struct{}
type tableKind struct{}
type unionKind struct{}
type xunionKind struct{}

var Kinds = struct {
	Const     constKind
	Bits      bitsKind
	Enum      enumKind
	Interface interfaceKind
	Struct    structKind
	Table     tableKind
	Union     unionKind
	XUnion    xunionKind
}{}

type Decl interface{}

type Type struct {
	Decl                string
	Dtor                string
	LLDecl              string
	LLDtor              string
	OvernetEmbeddedDecl string
	OvernetEmbeddedDtor string
	DeclType            types.DeclType
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
	Namespace string
	Type      string
	Name      string
	Mask      string
	MaskName  string
	Members   []BitsMember
	Kind      bitsKind
}

type BitsMember struct {
	Name  string
	Value string
}

type Enum struct {
	Namespace string
	Type      string
	Name      string
	Members   []EnumMember
	Kind      enumKind
}

type EnumMember struct {
	Name  string
	Value string
}

type Union struct {
	types.Attributes
	Namespace    string
	Name         string
	TableType    string
	Members      []UnionMember
	Size         int
	MaxHandles   int
	MaxOutOfLine int
	Kind         unionKind
}

type UnionMember struct {
	types.Attributes
	Type        Type
	Name        string
	StorageName string
	TagName     string
	Offset      int
}

type XUnion struct {
	types.Attributes
	Namespace    string
	Name         string
	TableType    string
	Members      []XUnionMember
	Size         int
	MaxHandles   int
	MaxOutOfLine int
	Kind         xunionKind
}

type XUnionMember struct {
	types.Attributes
	Ordinal     int
	Type        Type
	Name        string
	StorageName string
	TagName     string
	Offset      int
}

type Table struct {
	types.Attributes
	Namespace      string
	Name           string
	TableType      string
	Members        []TableMember
	Size           int
	BiggestOrdinal int
	MaxHandles     int
	MaxOutOfLine   int
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
	Namespace    string
	Name         string
	TableType    string
	Members      []StructMember
	Size         int
	MaxHandles   int
	MaxOutOfLine int
	Kind         structKind
}

type StructMember struct {
	types.Attributes
	Type         Type
	Name         string
	DefaultValue string
	Offset       int
}

func (s Struct) NeedsEncodeDecode() bool {
	return s.MaxHandles > 0 || s.MaxOutOfLine > 0
}

type Interface struct {
	types.Attributes
	Namespace             string
	Name                  string
	ClassName             string
	ServiceName           string
	ProxyName             string
	StubName              string
	EventSenderName       string
	SyncName              string
	SyncProxyName         string
	Methods               []Method
	HasEvents             bool
	StackAllocEventBuffer bool
	Kind                  interfaceKind
}

type Method struct {
	types.Attributes
	Ordinal              types.Ordinal
	OrdinalName          string
	GenOrdinal           types.Ordinal
	GenOrdinalName       string
	Name                 string
	NameInLowerSnakeCase string
	HasRequest           bool
	Request              []Parameter
	RequestSize          int
	RequestTypeName      string
	RequestMaxHandles    int
	RequestMaxOutOfLine  int
	HasResponse          bool
	Response             []Parameter
	ResponseSize         int
	ResponseTypeName     string
	ResponseMaxHandles   int
	ResponseMaxOutOfLine int
	CallbackType         string
	ResponseHandlerType  string
	ResponderType        string
	Transitional         bool
	LLProps              LLProps
}

// LLProps contain properties of a method specific to llcpp
type LLProps struct {
	InterfaceName      string
	CBindingCompatible bool
	LinearizeRequest   bool
	LinearizeResponse  bool
	StackAllocRequest  bool
	StackAllocResponse bool
	EncodeRequest      bool
	DecodeResponse     bool
}

type Parameter struct {
	Type   Type
	Name   string
	Offset int
}

type Root struct {
	PrimaryHeader          string
	Headers                []string
	LLHeaders              []string
	OvernetEmbeddedHeaders []string
	HandleTypes            []string
	Library                types.LibraryIdentifier
	LibraryReversed        types.LibraryIdentifier
	Decls                  []Decl
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
	handleTypes  map[types.HandleSubtype]bool
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

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, ext, appendNamespace string) string {
	val := types.ParseCompoundIdentifier(eci)
	strs := []string{}
	if c.isInExternalLibrary(val) {
		strs = append(strs, formatNamespace(val.Library, appendNamespace))
	}
	strs = append(strs, changeIfReserved(val.Name, ext))
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
		v := c.compileCompoundIdentifier(val.Identifier, "", appendNamespace)
		if t != nil && (t.DeclType == types.BitsDeclType || t.DeclType == types.EnumDeclType) {
			v = fmt.Sprintf("%s::%s", t.Decl, v)
		}
		return v
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
		r.OvernetEmbeddedDecl = r.Decl
		r.OvernetEmbeddedDtor = r.Dtor
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
		r.OvernetEmbeddedDecl = r.Decl
		r.OvernetEmbeddedDtor = r.Dtor
	case types.StringType:
		r.LLDecl = "::fidl::StringView"
		if val.Nullable {
			r.Decl = "::fidl::StringPtr"
			r.Dtor = "~StringPtr"
		} else {
			r.Decl = "::std::string"
			r.Dtor = "~basic_string"
		}
		r.OvernetEmbeddedDecl = r.Decl
		r.OvernetEmbeddedDtor = r.Dtor
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
		r.OvernetEmbeddedDecl = fmt.Sprintf("::overnet::ClosedPtr<::overnet::Zx%s>",
			strings.Title(fmt.Sprintf("%s", val.HandleSubtype)))
		r.OvernetEmbeddedDtor = "~ClosedPtr"
	case types.RequestType:
		r.Decl = fmt.Sprintf("::fidl::InterfaceRequest<%s>",
			c.compileCompoundIdentifier(val.RequestSubtype, "", ""))
		r.LLDecl = "::zx::channel"
		r.Dtor = "~InterfaceRequest"
		r.LLDtor = "~channel"
		r.OvernetEmbeddedDecl = fmt.Sprintf("::std::unique_ptr<%s_Request>",
			c.compileCompoundIdentifier(val.RequestSubtype, "", "embedded"))
		r.OvernetEmbeddedDtor = "~unique_ptr"
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.LLDecl = r.Decl
		r.OvernetEmbeddedDecl = r.Decl
	case types.IdentifierType:
		t := c.compileCompoundIdentifier(val.Identifier, "", "")
		tEmbbeded := c.compileCompoundIdentifier(val.Identifier, "", "embedded")
		declType, ok := (*c.decls)[val.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", val.Identifier)
		}
		switch declType {
		case types.BitsDeclType:
			fallthrough
		case types.ConstDeclType:
			fallthrough
		case types.EnumDeclType:
			fallthrough
		case types.StructDeclType:
			fallthrough
		case types.TableDeclType:
			fallthrough
		case types.UnionDeclType:
			fallthrough
		case types.XUnionDeclType:
			if val.Nullable {
				r.Decl = fmt.Sprintf("::std::unique_ptr<%s>", t)
				if declType == types.XUnionDeclType {
					r.LLDecl = fmt.Sprintf("%s", t)
				} else {
					r.LLDecl = fmt.Sprintf("%s*", t)
				}
				r.Dtor = "~unique_ptr"
				r.OvernetEmbeddedDecl = fmt.Sprintf("::std::unique_ptr<%s>", tEmbbeded)
				r.OvernetEmbeddedDtor = "~unique_ptr"
			} else {
				r.Decl = t
				r.LLDecl = r.Decl
				r.Dtor = formatDestructor(val.Identifier)
				r.LLDtor = r.Dtor
				r.OvernetEmbeddedDecl = tEmbbeded
				r.OvernetEmbeddedDtor = r.Dtor
			}
		case types.InterfaceDeclType:
			r.Decl = fmt.Sprintf("::fidl::InterfaceHandle<%s>", t)
			r.Dtor = fmt.Sprintf("~InterfaceHandle")
			r.LLDecl = "::zx::channel"
			r.LLDtor = "~channel"
			r.OvernetEmbeddedDecl = fmt.Sprintf("::std::unique_ptr<%s_Proxy>", tEmbbeded)
			r.OvernetEmbeddedDtor = "~unique_ptr"
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
		Namespace: c.namespace,
		Type:      c.compileType(val.Type).Decl,
		Name:      c.compileCompoundIdentifier(val.Name, "", appendNamespace),
		Mask:      val.Mask,
		MaskName:  c.compileCompoundIdentifier(val.Name, "Mask", appendNamespace),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, BitsMember{
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
				Decl: "char",
			},
			Name:  c.compileCompoundIdentifier(val.Name, "[]", appendNamespace),
			Value: c.compileConstant(val.Value, nil, val.Type, appendNamespace),
		}
	} else {
		t := c.compileType(val.Type)
		return Const{
			Attributes: val.Attributes,
			Extern:     false,
			Decorator:  "constexpr",
			Type:       t,
			Name:       c.compileCompoundIdentifier(val.Name, "", appendNamespace),
			Value:      c.compileConstant(val.Value, &t, val.Type, appendNamespace),
		}
	}
}

func (c *compiler) compileEnum(val types.Enum, appendNamespace string) Enum {
	r := Enum{
		Namespace: c.namespace,
		Type:      c.compilePrimitiveSubtype(val.Type),
		Name:      c.compileCompoundIdentifier(val.Name, "", appendNamespace),
		Members:   []EnumMember{},
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			Name: changeIfReserved(v.Name, ""),
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

func (c *compiler) maxHandlesFromParameterArray(val []types.Parameter) int {
	numHandles := int64(0)
	for _, v := range val {
		numHandles += int64(v.MaxHandles)
	}
	if numHandles > math.MaxUint32 {
		return math.MaxUint32
	} else {
		return int(numHandles)
	}
}

func (c *compiler) maxOutOfLineFromParameterArray(val []types.Parameter) int {
	maxOutOfLine := int64(0)
	for _, v := range val {
		maxOutOfLine += int64(v.MaxOutOfLine)
	}
	if maxOutOfLine > math.MaxUint32 {
		return math.MaxUint32
	} else {
		return int(maxOutOfLine)
	}
}

func (m Method) NewLLProps(r Interface) LLProps {
	return LLProps{
		InterfaceName: r.Name,
		// If the response is not inline, then we cannot generate an out-parameter-style binding,
		// because the out-of-line pointers would outlive their underlying managed storage.
		CBindingCompatible: m.ResponseMaxOutOfLine == 0,
		LinearizeRequest:   len(m.Request) > 0 && m.RequestMaxOutOfLine > 0,
		LinearizeResponse:  len(m.Response) > 0 && m.ResponseMaxOutOfLine > 0,
		StackAllocRequest:  len(m.Request) == 0 || (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize,
		StackAllocResponse: len(m.Response) == 0 || (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize,
		EncodeRequest:      m.RequestMaxOutOfLine > 0 || m.RequestMaxHandles > 0,
		DecodeResponse:     m.ResponseMaxOutOfLine > 0 || m.ResponseMaxHandles > 0,
	}
}

func (c *compiler) compileInterface(val types.Interface) Interface {
	r := Interface{
		Attributes:      val.Attributes,
		Namespace:       c.namespace,
		Name:            c.compileCompoundIdentifier(val.Name, "", ""),
		ClassName:       c.compileCompoundIdentifier(val.Name, "_clazz", ""),
		ServiceName:     val.GetServiceName(),
		ProxyName:       c.compileCompoundIdentifier(val.Name, "_Proxy", ""),
		StubName:        c.compileCompoundIdentifier(val.Name, "_Stub", ""),
		EventSenderName: c.compileCompoundIdentifier(val.Name, "_EventSender", ""),
		SyncName:        c.compileCompoundIdentifier(val.Name, "_Sync", ""),
		SyncProxyName:   c.compileCompoundIdentifier(val.Name, "_SyncProxy", ""),
	}

	hasEvents := false
	stackAllocEventBuffer := true
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
		_, transitional := v.LookupAttribute("Transitional")
		m := Method{
			Attributes:           v.Attributes,
			Ordinal:              v.Ordinal,
			OrdinalName:          fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
			GenOrdinal:           v.GenOrdinal,
			GenOrdinalName:       fmt.Sprintf("k%s_%s_GenOrdinal", r.Name, v.Name),
			Name:                 name,
			NameInLowerSnakeCase: common.ToSnakeCase(name),
			HasRequest:           v.HasRequest,
			Request:              c.compileParameterArray(v.Request),
			RequestSize:          v.RequestSize,
			RequestTypeName:      fmt.Sprintf("%s_%s%sRequestTable", c.symbolPrefix, r.Name, v.Name),
			RequestMaxHandles:    c.maxHandlesFromParameterArray(v.Request),
			RequestMaxOutOfLine:  c.maxOutOfLineFromParameterArray(v.Request),
			HasResponse:          v.HasResponse,
			Response:             c.compileParameterArray(v.Response),
			ResponseSize:         v.ResponseSize,
			ResponseTypeName:     fmt.Sprintf("%s_%s%s%s", c.symbolPrefix, r.Name, v.Name, responseTypeNameSuffix),
			ResponseMaxHandles:   c.maxHandlesFromParameterArray(v.Response),
			ResponseMaxOutOfLine: c.maxOutOfLineFromParameterArray(v.Response),
			CallbackType:         callbackType,
			ResponseHandlerType:  fmt.Sprintf("%s_%s_ResponseHandler", r.Name, v.Name),
			ResponderType:        fmt.Sprintf("%s_%s_Responder", r.Name, v.Name),
			Transitional:         transitional,
		}
		m.LLProps = m.NewLLProps(r)
		r.Methods = append(r.Methods, m)
		if !v.HasRequest {
			stackAllocEventBuffer = stackAllocEventBuffer && m.LLProps.StackAllocResponse
		}
	}
	r.HasEvents = hasEvents
	r.StackAllocEventBuffer = stackAllocEventBuffer

	return r
}

func (c *compiler) compileStructMember(val types.StructMember, appendNamespace string) StructMember {
	t := c.compileType(val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type, appendNamespace)
	}

	return StructMember{
		val.Attributes,
		t,
		changeIfReserved(val.Name, ""),
		defaultValue,
		val.Offset,
	}
}

func (c *compiler) compileStruct(val types.Struct, appendNamespace string) Struct {
	name := c.compileCompoundIdentifier(val.Name, "", appendNamespace)
	r := Struct{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         name,
		TableType:    c.compileTableType(val.Name),
		Members:      []StructMember{},
		Size:         val.Size,
		MaxHandles:   val.MaxHandles,
		MaxOutOfLine: val.MaxOutOfLine,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v, appendNamespace))
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

type byOrdinal []TableMember

func (m byOrdinal) Len() int {
	return len(m)
}
func (m byOrdinal) Swap(i, j int) {
	m[i], m[j] = m[j], m[i]
}
func (m byOrdinal) Less(i, j int) bool {
	return m[i].Ordinal < m[j].Ordinal
}

func (c *compiler) compileTable(val types.Table, appendNamespace string) Table {
	name := c.compileCompoundIdentifier(val.Name, "", appendNamespace)
	r := Table{
		Attributes:     val.Attributes,
		Namespace:      c.namespace,
		Name:           name,
		TableType:      c.compileTableType(val.Name),
		Members:        nil,
		Size:           val.Size,
		BiggestOrdinal: 0,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		m := c.compileTableMember(v, appendNamespace)
		r.BiggestOrdinal = m.Ordinal
		r.Members = append(r.Members, m)
	}

	sort.Sort(byOrdinal(r.Members))

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	n := changeIfReserved(val.Name, "")
	return UnionMember{
		Attributes:  val.Attributes,
		Type:        c.compileType(val.Type),
		Name:        n,
		StorageName: changeIfReserved(val.Name, "_"),
		TagName:     fmt.Sprintf("k%s", common.ToUpperCamelCase(n)),
		Offset:      val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	name := c.compileCompoundIdentifier(val.Name, "", "")
	r := Union{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         changeIfReserved(types.Identifier(name), ""),
		TableType:    c.compileTableType(val.Name),
		Members:      []UnionMember{},
		Size:         val.Size,
		MaxHandles:   val.MaxHandles,
		MaxOutOfLine: val.MaxOutOfLine,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func (c *compiler) compileXUnionMember(val types.XUnionMember) XUnionMember {
	n := changeIfReserved(val.Name, "")
	return XUnionMember{
		Attributes:  val.Attributes,
		Ordinal:     val.Ordinal,
		Type:        c.compileType(val.Type),
		Name:        n,
		StorageName: changeIfReserved(val.Name, "_"),
		TagName:     fmt.Sprintf("k%s", common.ToUpperCamelCase(n)),
		Offset:      val.Offset,
	}
}

func (c *compiler) compileXUnion(val types.XUnion) XUnion {
	name := c.compileCompoundIdentifier(val.Name, "", "")
	r := XUnion{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         name,
		TableType:    c.compileTableType(val.Name),
		Size:         val.Size,
		MaxHandles:   val.MaxHandles,
		MaxOutOfLine: val.MaxOutOfLine,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileXUnionMember(v))
	}

	return r
}

func Compile(r types.Root) Root {
	root := Root{}
	library := make(types.LibraryIdentifier, 0)
	raw_library := make(types.LibraryIdentifier, 0)
	for _, identifier := range types.ParseLibraryName(r.Name) {
		safe_name := changeIfReserved(identifier, "")
		library = append(library, types.Identifier(safe_name))
		raw_library = append(raw_library, identifier)
	}
	c := compiler{
		formatNamespace(library, ""),
		formatLibraryPrefix(raw_library),
		&r.Decls,
		types.ParseLibraryName(r.Name),
		make(map[types.HandleSubtype]bool),
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

	for _, v := range r.Interfaces {
		d := c.compileInterface(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Structs {
		d := c.compileStruct(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Tables {
		d := c.compileTable(v, "")
		decls[v.Name] = &d
	}

	for _, v := range r.Unions {
		d := c.compileUnion(v)
		decls[v.Name] = &d
	}

	for _, v := range r.XUnions {
		d := c.compileXUnion(v)
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
		root.Headers = append(root.Headers, fmt.Sprintf("%s/cpp/fidl.h", formatLibraryPath(libraryIdent)))
		root.LLHeaders = append(root.LLHeaders, fmt.Sprintf("%s/llcpp/fidl.h", formatLibraryPath(libraryIdent)))
		root.OvernetEmbeddedHeaders = append(root.OvernetEmbeddedHeaders, fmt.Sprintf("%s/cpp/overnet_embedded.h", formatLibraryPath(libraryIdent)))
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
