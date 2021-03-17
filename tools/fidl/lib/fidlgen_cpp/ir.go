// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"sort"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// This value needs to be kept in sync with the one defined in
// zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
const llcppMaxStackAllocSize = 512
const channelMaxMessageSize = 65536

// These are used in header/impl templates to select the correct type-specific template
type bitsKind struct{}
type constKind struct{}
type enumKind struct{}
type protocolKind struct{}
type serviceKind struct{}
type structKind struct{}
type tableKind struct{}
type unionKind struct{}

var Kinds = struct {
	Const    constKind
	Bits     bitsKind
	Enum     enumKind
	Protocol protocolKind
	Service  serviceKind
	Struct   structKind
	Table    tableKind
	Union    unionKind
}{}

// A Decl is any type with a .Kind field.
type Decl interface{}

type familyKind namespacedEnumMember

type familyKinds struct {
	// TrivialCopy identifies values for whom a copy is trivial (like integers)
	TrivialCopy familyKind

	// Reference identifies values with a non trivial copy for which we use a
	// reference on the caller argument.
	Reference familyKind

	// String identifies string values for which we can use a const reference
	// and for which we can optimize the field construction.
	String familyKind

	// Vector identifies vector values for which we can use a reference and for
	// which we can optimize the field construction.
	Vector familyKind
}

// FamilyKinds are general categories identifying what operation we should use
// to pass a value without a move (LLCPP). It also defines the way we should
// initialize a field.
var FamilyKinds = namespacedEnum(familyKinds{}).(familyKinds)

type typeKind namespacedEnumMember

type typeKinds struct {
	Array     typeKind
	Vector    typeKind
	String    typeKind
	Handle    typeKind
	Request   typeKind
	Primitive typeKind
	Bits      typeKind
	Enum      typeKind
	Const     typeKind
	Struct    typeKind
	Table     typeKind
	Union     typeKind
	Protocol  typeKind
}

// TypeKinds are the kinds of declarations (arrays, primitives, structs, ...).
var TypeKinds = namespacedEnum(typeKinds{}).(typeKinds)

type Type struct {
	TypeName

	WirePointer bool

	// Defines what operation we should use to pass a value without a move (LLCPP). It also
	// defines the way we should initialize a field.
	WireFamily familyKind

	// NeedsDtor indicates whether this type needs to be destructed explicitely
	// or not.
	NeedsDtor bool

	Kind typeKind

	IsResource bool

	DeclarationName fidl.EncodedCompoundIdentifier

	// Set iff IsArray || IsVector
	ElementType *Type
	// Valid iff IsArray
	ElementCount int
}

// IsPrimitiveType returns true if this type is primitive.
func (t *Type) IsPrimitiveType() bool {
	return t.Kind == TypeKinds.Primitive || t.Kind == TypeKinds.Bits || t.Kind == TypeKinds.Enum
}

// WireArgumentDeclaration returns the argument declaration for this type for the wire variant.
func (t *Type) WireArgumentDeclaration(n string) string {
	switch t.WireFamily {
	case FamilyKinds.TrivialCopy:
		return t.String() + " " + n
	case FamilyKinds.Reference, FamilyKinds.Vector:
		return t.String() + "& " + n
	case FamilyKinds.String:
		return "const " + t.String() + "& " + n
	default:
		panic(fmt.Sprintf("Unknown wire family kind %v", t.WireFamily))
	}
}

// WireInitMessage returns message field initialization for the wire variant.
func (t *Type) WireInitMessage(n string) string {
	switch t.WireFamily {
	case FamilyKinds.TrivialCopy:
		return fmt.Sprintf("%s(%s)", n, n)
	case FamilyKinds.Reference:
		return fmt.Sprintf("%s(std::move(%s))", n, n)
	case FamilyKinds.String:
		return fmt.Sprintf("%s(::fidl::unowned_ptr_t<const char>(%s.data()), %s.size())",
			n, n, n)
	case FamilyKinds.Vector:
		return fmt.Sprintf("%s(::fidl::unowned_ptr_t<%s>(%s.mutable_data()), %s.count())",
			n, t.ElementType, n, n)
	default:
		panic(fmt.Sprintf("Unknown wire family kind %v", t.WireFamily))

	}
}

type Member interface {
	NameAndType() (string, Type)
}

type Union struct {
	fidl.Attributes
	fidl.Strictness
	fidl.Resourceness
	DeclName
	TableType    string
	Members      []UnionMember
	InlineSize   int
	MaxHandles   int
	MaxOutOfLine int
	Result       *Result
	HasPointer   bool

	// Kind should be default initialized.
	Kind unionKind
}

type UnionMember struct {
	fidl.Attributes
	Ordinal           uint64
	Type              Type
	Name              string
	StorageName       string
	TagName           string
	Offset            int
	HandleInformation *HandleInformation
}

func (um UnionMember) UpperCamelCaseName() string {
	return fidl.ToUpperCamelCase(um.Name)
}

func (um UnionMember) NameAndType() (string, Type) {
	return um.Name, um.Type
}

type TableFrameItem *TableMember

type Table struct {
	fidl.Attributes
	fidl.Resourceness
	DeclName
	TableType      string
	Members        []TableMember
	InlineSize     int
	BiggestOrdinal int
	MaxHandles     int
	MaxOutOfLine   int
	ByteBufferType string
	HasPointer     bool

	// FrameItems stores the members in ordinal order; "null" for reserved.
	FrameItems []TableFrameItem

	// Kind should be default initialized.
	Kind tableKind
}

type TableMember struct {
	fidl.Attributes
	Type               Type
	Name               string
	DefaultValue       ConstantValue
	Ordinal            int
	FieldPresenceIsSet string
	FieldPresenceSet   string
	FieldPresenceClear string
	FieldDataName      string
	MethodHasName      string
	MethodClearName    string
	ValueUnionName     string
	HandleInformation  *HandleInformation
}

func (tm TableMember) NameAndType() (string, Type) {
	return tm.Name, tm.Type
}

// protocolInner contains information about a Protocol that should be
// filled out by the compiler.
type protocolInner struct {
	fidl.Attributes
	DeclName
	ClassName           string
	ServiceName         string
	ProxyName           DeclVariant
	StubName            DeclVariant
	EventSenderName     DeclVariant
	SyncName            DeclVariant
	SyncProxyName       DeclVariant
	RequestEncoderName  DeclVariant
	RequestDecoderName  DeclVariant
	ResponseEncoderName DeclVariant
	ResponseDecoderName DeclVariant
	ByteBufferType      string
	Methods             []Method
	FuzzingName         string
	TestBase            DeclName
}

// Protocol should be created using protocolInner.build().
type Protocol struct {
	protocolInner

	// OneWayMethods contains the list of one-way (i.e. fire-and-forget) methods
	// in the protocol.
	OneWayMethods []Method

	// TwoWayMethods contains the list of two-way (i.e. has both request and
	// response) methods in the protocol.
	TwoWayMethods []Method

	// ClientMethods contains the list of client-initiated methods (i.e. any
	// interaction that is not an event). It is the union of one-way and two-way
	// methods.
	ClientMethods []Method

	// Events contains the list of events (i.e. initiated by servers)
	// in the protocol.
	Events []Method

	// Kind should always be default initialized.
	Kind protocolKind
}

func (p *Protocol) Name() string {
	return p.Wire.Name() // TODO: not the wire name, maybe?
}

func (p *Protocol) NaturalType() string {
	return string(p.Natural.Type())
}

func (p *Protocol) WireType() string {
	return string(p.Wire.Type())
}

func (inner protocolInner) build() *Protocol {
	type kinds []methodKind

	filterBy := func(kinds kinds) []Method {
		var out []Method
		for _, m := range inner.Methods {
			k := m.methodKind()
			for _, want := range kinds {
				if want == k {
					out = append(out, m)
				}
			}
		}
		return out
	}

	return &Protocol{
		protocolInner: inner,
		OneWayMethods: filterBy(kinds{oneWayMethod}),
		TwoWayMethods: filterBy(kinds{twoWayMethod}),
		ClientMethods: filterBy(kinds{oneWayMethod, twoWayMethod}),
		Events:        filterBy(kinds{eventMethod}),
	}
}

type Service struct {
	fidl.Attributes
	DeclName
	ServiceName string
	Members     []ServiceMember

	// Kind should be default initialized.
	Kind serviceKind
}

type ServiceMember struct {
	fidl.Attributes
	ProtocolType DeclName
	Name         string
	MethodName   string
}

// methodInner contains information about a Method that should be filled out by
// the compiler.
type methodInner struct {
	// Private fields used to construct Method.
	protocolName      DeclName
	requestTypeShape  fidl.TypeShape
	responseTypeShape fidl.TypeShape
	// Public fields.
	fidl.Attributes
	Name                string
	Ordinal             uint64
	HasRequest          bool
	Request             []Parameter
	RequestCodingTable  DeclName
	HasResponse         bool
	Response            []Parameter
	ResponseCodingTable DeclName
	Transitional        bool
	Result              *Result
}

func fidlAlign(size int) int {
	return (size + 7) & ^7
}

type boundedness bool

const (
	boundednessBounded   = true
	boundednessUnbounded = false
)

func byteBufferType(primarySize int, maxOutOfLine int, boundedness boundedness) string {
	var sizeString string
	var size int
	if boundedness == boundednessUnbounded || primarySize+maxOutOfLine > channelMaxMessageSize {
		sizeString = "ZX_CHANNEL_MAX_MSG_BYTES"
		size = channelMaxMessageSize
	} else {
		size = fidlAlign(primarySize + maxOutOfLine)
		sizeString = fmt.Sprintf("%d", size)
	}

	if size > llcppMaxStackAllocSize {
		return fmt.Sprintf("::fidl::internal::BoxedMessageBuffer<%s>", sizeString)
	}
	return fmt.Sprintf("::fidl::internal::InlineMessageBuffer<%s>", sizeString)
}

// Method should be created using methodInner.build().
// TODO: Consider factoring out common fields between Request and Response.
type Method struct {
	methodInner
	NameInLowerSnakeCase string
	// The name of a constant that defines the ordinal value.
	OrdinalName             string
	RequestSize             int
	RequestMaxHandles       int
	RequestMaxOutOfLine     int
	RequestByteBufferType   string
	RequestPadding          bool
	RequestFlexible         bool
	RequestHasPointer       bool
	RequestIsResource       bool
	ResponseSize            int
	ResponseMaxHandles      int
	ResponseMaxOutOfLine    int
	ResponseByteBufferType  string
	ResponseReceivedMaxSize int
	ResponsePadding         bool
	ResponseFlexible        bool
	ResponseHasPointer      bool
	ResponseIsResource      bool
	CallbackType            string
	ResponseHandlerType     string
	ResponderType           string
	LLProps                 LLProps
}

func (inner methodInner) build() Method {
	requestIsResource := false
	for _, p := range inner.Request {
		if p.Type.IsResource {
			requestIsResource = true
			break
		}
	}
	responseIsResource := false
	for _, p := range inner.Response {
		if p.Type.IsResource {
			responseIsResource = true
			break
		}
	}

	callbackType := ""
	if inner.HasResponse {
		callbackType = changeIfReserved(fidl.Identifier(inner.Name + "Callback"))
	}

	var computedResponseReceivedMaxSize int
	if inner.responseTypeShape.HasFlexibleEnvelope {
		computedResponseReceivedMaxSize = (1 << 32) - 1
	} else {
		computedResponseReceivedMaxSize = inner.responseTypeShape.InlineSize + inner.responseTypeShape.MaxOutOfLine
	}

	var responseBoundedness boundedness = boundednessBounded
	if inner.responseTypeShape.HasFlexibleEnvelope {
		responseBoundedness = boundednessUnbounded
	}

	m := Method{
		methodInner:             inner,
		NameInLowerSnakeCase:    fidl.ToSnakeCase(inner.Name),
		OrdinalName:             fmt.Sprintf("k%s_%s_Ordinal", inner.protocolName.Natural.Name(), inner.Name),
		RequestSize:             inner.requestTypeShape.InlineSize,
		RequestMaxHandles:       inner.requestTypeShape.MaxHandles,
		RequestMaxOutOfLine:     inner.requestTypeShape.MaxOutOfLine,
		RequestByteBufferType:   byteBufferType(inner.requestTypeShape.InlineSize, inner.requestTypeShape.MaxOutOfLine, boundednessBounded),
		RequestPadding:          inner.requestTypeShape.HasPadding,
		RequestFlexible:         inner.requestTypeShape.HasFlexibleEnvelope,
		RequestHasPointer:       inner.requestTypeShape.Depth > 0,
		RequestIsResource:       requestIsResource,
		ResponseSize:            inner.responseTypeShape.InlineSize,
		ResponseMaxHandles:      inner.responseTypeShape.MaxHandles,
		ResponseMaxOutOfLine:    inner.responseTypeShape.MaxOutOfLine,
		ResponseByteBufferType:  byteBufferType(inner.responseTypeShape.InlineSize, inner.responseTypeShape.MaxOutOfLine, responseBoundedness),
		ResponseReceivedMaxSize: computedResponseReceivedMaxSize,
		ResponsePadding:         inner.responseTypeShape.HasPadding,
		ResponseFlexible:        inner.responseTypeShape.HasFlexibleEnvelope,
		ResponseHasPointer:      inner.responseTypeShape.Depth > 0,
		ResponseIsResource:      responseIsResource,
		CallbackType:            callbackType,
		ResponseHandlerType:     fmt.Sprintf("%s_%s_ResponseHandler", inner.protocolName.Natural.Name(), inner.Name),
		ResponderType:           fmt.Sprintf("%s_%s_Responder", inner.protocolName.Natural.Name(), inner.Name),
	}
	m.LLProps = LLProps{
		ProtocolName:      inner.protocolName.Wire,
		LinearizeRequest:  len(inner.Request) > 0 && inner.requestTypeShape.Depth > 0,
		LinearizeResponse: len(inner.Response) > 0 && inner.responseTypeShape.Depth > 0,
		ClientContext:     m.buildLLContextProps(clientContext),
		ServerContext:     m.buildLLContextProps(serverContext),
	}
	return m
}

type methodKind int

const (
	oneWayMethod = methodKind(iota)
	twoWayMethod
	eventMethod
)

func (m *Method) methodKind() methodKind {
	if m.HasRequest {
		if m.HasResponse {
			return twoWayMethod
		}
		return oneWayMethod
	}
	if !m.HasResponse {
		panic("A method should have at least either a request or a response")
	}
	return eventMethod
}

// LLContextProps contain context-dependent properties of a method specific to llcpp.
// Context is client (write request and read response) or server (read request and write response).
type LLContextProps struct {
	// Should the request be allocated on the stack, in the managed flavor.
	StackAllocRequest bool
	// Should the response be allocated on the stack, in the managed flavor.
	StackAllocResponse bool
	// Total number of bytes of stack used for storing the request.
	StackUseRequest int
	// Total number of bytes of stack used for storing the response.
	StackUseResponse int
}

// LLProps contain properties of a method specific to llcpp
type LLProps struct {
	ProtocolName      DeclVariant
	LinearizeRequest  bool
	LinearizeResponse bool
	ClientContext     LLContextProps
	ServerContext     LLContextProps
}

type Parameter struct {
	Type              Type
	Name              string
	Offset            int
	HandleInformation *HandleInformation
}

func (p Parameter) NameAndType() (string, Type) {
	return p.Name, p.Type
}

type Root struct {
	PrimaryHeader          string
	IncludeStem            string
	Headers                []string
	FuzzerHeaders          []string
	HandleTypes            []string
	Library                fidl.LibraryIdentifier
	LibraryReversed        fidl.LibraryIdentifier
	LibraryCommonNamespace Namespace
	Decls                  []Decl
}

// Holds information about error results on methods
type Result struct {
	ValueMembers    []Parameter
	ResultDecl      DeclName
	ErrorDecl       TypeName
	ValueDecl       TypeVariant
	ValueStructDecl TypeName
	ValueTupleDecl  TypeVariant
}

func (r Result) ValueArity() int {
	return len(r.ValueMembers)
}

func (m *Method) CallbackWrapper() string {
	return "fit::function"
}

var primitiveTypes = map[fidl.PrimitiveSubtype]string{
	fidl.Bool:    "bool",
	fidl.Int8:    "int8_t",
	fidl.Int16:   "int16_t",
	fidl.Int32:   "int32_t",
	fidl.Int64:   "int64_t",
	fidl.Uint8:   "uint8_t",
	fidl.Uint16:  "uint16_t",
	fidl.Uint32:  "uint32_t",
	fidl.Uint64:  "uint64_t",
	fidl.Float32: "float",
	fidl.Float64: "double",
}

// TypeNameForPrimitive returns the C++ name of a FIDL primitive type.
func TypeNameForPrimitive(t fidl.PrimitiveSubtype) TypeName {
	return PrimitiveTypeName(primitiveTypes[t])
}

type identifierTransform bool

const (
	keepPartIfReserved   identifierTransform = false
	changePartIfReserved identifierTransform = true
)

func libraryParts(library fidl.LibraryIdentifier, identifierTransform identifierTransform) []string {
	parts := []string{}
	for _, part := range library {
		if identifierTransform == changePartIfReserved {
			parts = append(parts, changeIfReserved(part))
		} else {
			parts = append(parts, string(part))
		}
	}
	return parts
}

func oldLlLibraryNamepace(library fidl.LibraryIdentifier) Namespace {
	parts := libraryParts(library, changePartIfReserved)
	// Avoid user-defined llcpp library colliding with the llcpp namespace, by appending underscore.
	if len(parts) > 0 && parts[0] == "llcpp" {
		parts[0] = "llcpp_"
	}
	parts = append([]string{"llcpp"}, parts...)
	return Namespace(parts)
}

func wireLibraryNamespace(library fidl.LibraryIdentifier) Namespace {
	return commonLibraryNamespace(library).Append("wire")
}

func hlLibraryNamespace(library fidl.LibraryIdentifier) Namespace {
	parts := libraryParts(library, changePartIfReserved)
	return Namespace(parts)
}
func naturalLibraryNamespace(library fidl.LibraryIdentifier) Namespace {
	return hlLibraryNamespace(library)
}

func formatLibrary(library fidl.LibraryIdentifier, sep string, identifierTransform identifierTransform) string {
	name := strings.Join(libraryParts(library, identifierTransform), sep)
	return changeIfReserved(fidl.Identifier(name))
}

func commonLibraryNamespace(library fidl.LibraryIdentifier) Namespace {
	return Namespace([]string{formatLibrary(library, "_", keepPartIfReserved)})
}

func formatLibraryPrefix(library fidl.LibraryIdentifier) string {
	return formatLibrary(library, "_", keepPartIfReserved)
}

func formatLibraryPath(library fidl.LibraryIdentifier) string {
	return formatLibrary(library, "/", keepPartIfReserved)
}

type libraryNamespaceFunc func(fidl.LibraryIdentifier) Namespace

func codingTableName(ident fidl.EncodedCompoundIdentifier) string {
	ci := fidl.ParseCompoundIdentifier(ident)
	return formatLibrary(ci.Library, "_", keepPartIfReserved) + "_" + string(ci.Name) + string(ci.Member)
}

type compiler struct {
	symbolPrefix     string
	decls            fidl.DeclInfoMap
	library          fidl.LibraryIdentifier
	handleTypes      map[fidl.HandleSubtype]struct{}
	naturalNamespace libraryNamespaceFunc
	wireNamespace    libraryNamespaceFunc
	commonNamespace  libraryNamespaceFunc
	resultForStruct  map[fidl.EncodedCompoundIdentifier]*Result
	resultForUnion   map[fidl.EncodedCompoundIdentifier]*Result
}

func (c *compiler) isInExternalLibrary(ci fidl.CompoundIdentifier) bool {
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

func (c *compiler) compileDeclName(eci fidl.EncodedCompoundIdentifier) DeclName {
	ci := fidl.ParseCompoundIdentifier(eci)
	if ci.Member != fidl.Identifier("") {
		panic(fmt.Sprintf("unexpected compound identifier with member: %v", eci))
	}
	name := changeIfReserved(ci.Name)
	declInfo, ok := c.decls[eci]
	if !ok {
		panic(fmt.Sprintf("unknown identifier: %v", eci))
	}
	declType := declInfo.Type
	switch declType {
	case fidl.ConstDeclType, fidl.BitsDeclType, fidl.EnumDeclType, fidl.StructDeclType, fidl.TableDeclType, fidl.UnionDeclType:
		return DeclName{
			Natural: NewDeclVariant(name, c.naturalNamespace(ci.Library)),
			Wire:    NewDeclVariant(name, c.wireNamespace(ci.Library)),
		}
	case fidl.ProtocolDeclType, fidl.ServiceDeclType:
		return CommonDeclName(NewDeclVariant(name, c.commonNamespace(ci.Library)))
	}
	panic("Unknown decl type: " + string(declType))
}

func (c *compiler) compileTableType(eci fidl.EncodedCompoundIdentifier) string {
	val := fidl.ParseCompoundIdentifier(eci)
	if c.isInExternalLibrary(val) {
		panic(fmt.Sprintf("can't create table type for external identifier: %v", val))
	}

	return fmt.Sprintf("%s_%sTable", c.symbolPrefix, val.Name)
}

func (c *compiler) compilePrimitiveSubtype(val fidl.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

func (c *compiler) compileType(val fidl.Type) Type {
	r := Type{}
	switch val.Kind {
	case fidl.ArrayType:
		t := c.compileType(*val.ElementType)
		r.TypeName = t.TypeName.WithArrayTemplates("::std::array", "::fidl::Array", *val.ElementCount)
		r.WirePointer = t.WirePointer
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Array
		r.IsResource = t.IsResource
		r.ElementType = &t
		r.ElementCount = *val.ElementCount
	case fidl.VectorType:
		t := c.compileType(*val.ElementType)
		r.TypeName = t.TypeName.WithTemplates(
			map[bool]string{true: "::fidl::VectorPtr", false: "::std::vector"}[val.Nullable],
			"::fidl::VectorView")
		r.WireFamily = FamilyKinds.Vector
		r.WirePointer = t.WirePointer
		r.NeedsDtor = true
		r.Kind = TypeKinds.Vector
		r.IsResource = t.IsResource
		r.ElementType = &t
	case fidl.StringType:
		r.Wire = TypeVariant("::fidl::StringView")
		r.WireFamily = FamilyKinds.String
		if val.Nullable {
			r.Natural = TypeVariant("::fidl::StringPtr")
		} else {
			r.Natural = TypeVariant("::std::string")
		}
		r.NeedsDtor = true
		r.Kind = TypeKinds.String
	case fidl.HandleType:
		c.handleTypes[val.HandleSubtype] = struct{}{}
		r.TypeName = TypeNameForHandle(val.HandleSubtype)
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Handle
		r.IsResource = true
	case fidl.RequestType:
		r.TypeName = c.compileDeclName(val.RequestSubtype).TypeName().WithTemplates("::fidl::InterfaceRequest", "::fidl::ServerEnd")
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Request
		r.IsResource = true
	case fidl.PrimitiveType:
		r.TypeName = TypeNameForPrimitive(val.PrimitiveSubtype)
		r.WireFamily = FamilyKinds.TrivialCopy
		r.Kind = TypeKinds.Primitive
	case fidl.IdentifierType:
		name := c.compileDeclName(val.Identifier).TypeName()
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		declType := declInfo.Type
		if declType == fidl.ProtocolDeclType {
			r.TypeName = name.WithTemplates("::fidl::InterfaceHandle", "::fidl::ClientEnd")
			r.WireFamily = FamilyKinds.Reference
			r.NeedsDtor = true
			r.Kind = TypeKinds.Protocol
			r.IsResource = true
		} else {
			switch declType {
			case fidl.BitsDeclType:
				r.Kind = TypeKinds.Bits
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidl.EnumDeclType:
				r.Kind = TypeKinds.Enum
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidl.ConstDeclType:
				r.Kind = TypeKinds.Const
				r.WireFamily = FamilyKinds.Reference
			case fidl.StructDeclType:
				r.Kind = TypeKinds.Struct
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidl.TableDeclType:
				r.Kind = TypeKinds.Table
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidl.UnionDeclType:
				r.Kind = TypeKinds.Union
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
			default:
				panic(fmt.Sprintf("unknown declaration type: %v", declType))
			}

			if val.Nullable {
				r.TypeName = name.MapNatural(func(n TypeVariant) TypeVariant {
					return n.WithTemplate("::std::unique_ptr")
				}).MapWire(func(n TypeVariant) TypeVariant {
					if declType == fidl.UnionDeclType {
						return n
					} else {
						return n.WithTemplate("::fidl::tracking_ptr")
					}
				})
				r.NeedsDtor = true
			} else {
				r.TypeName = name
				r.NeedsDtor = true
			}
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}
	return r
}

func (c *compiler) compileParameterArray(val []fidl.Parameter) []Parameter {
	var params []Parameter = []Parameter{}
	for _, v := range val {
		params = append(params, Parameter{
			Type:              c.compileType(v.Type),
			Name:              changeIfReserved(v.Name),
			Offset:            v.FieldShapeV1.Offset,
			HandleInformation: c.fieldHandleInformation(&v.Type),
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

func (m Method) buildLLContextProps(context LLContext) LLContextProps {
	stackAllocRequest := false
	stackAllocResponse := false
	if context == clientContext {
		stackAllocRequest = len(m.Request) == 0 || (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize
		stackAllocResponse = len(m.Response) == 0 || (!m.ResponseFlexible && (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize)
	} else {
		stackAllocRequest = len(m.Request) == 0 || (!m.RequestFlexible && (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize)
		stackAllocResponse = len(m.Response) == 0 || (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize
	}

	stackUseRequest := 0
	stackUseResponse := 0
	if stackAllocRequest {
		stackUseRequest = m.RequestSize + m.RequestMaxOutOfLine
	}
	if stackAllocResponse {
		stackUseResponse = m.ResponseSize + m.ResponseMaxOutOfLine
	}
	return LLContextProps{
		StackAllocRequest:  stackAllocRequest,
		StackAllocResponse: stackAllocResponse,
		StackUseRequest:    stackUseRequest,
		StackUseResponse:   stackUseResponse,
	}
}

func (c *compiler) compileProtocol(val fidl.Protocol) *Protocol {
	protocolName := c.compileDeclName(val.Name)
	tableName := codingTableName(val.Name)
	tableBase := DeclName{
		Wire:    NewDeclVariant(tableName, protocolName.Wire.Namespace()),
		Natural: NewDeclVariant(tableName, protocolName.Natural.Namespace().Append("_internal")),
	}
	methods := []Method{}
	maxResponseSize := 0
	for _, v := range val.Methods {
		name := changeIfReserved(v.Name)
		requestTable := tableBase.AppendName(string(v.Name) + "RequestTable")
		responseTable := tableBase.AppendName(string(v.Name) + "ResponseTable")
		if !v.HasRequest {
			responseTable = tableBase.AppendName(string(v.Name) + "EventTable")

		}

		var result *Result
		if v.HasResponse && len(v.Response) == 1 {
			// If the method uses the error syntax, Response[0] will be a union
			// that was placed in c.resultForUnion. Otherwise, this will be nil.
			result = c.resultForUnion[v.Response[0].Type.Identifier]
		}

		method := methodInner{
			protocolName:        protocolName,
			requestTypeShape:    v.RequestTypeShapeV1,
			responseTypeShape:   v.ResponseTypeShapeV1,
			Attributes:          v.Attributes,
			Name:                name,
			Ordinal:             v.Ordinal,
			HasRequest:          v.HasRequest,
			Request:             c.compileParameterArray(v.Request),
			RequestCodingTable:  requestTable,
			HasResponse:         v.HasResponse,
			Response:            c.compileParameterArray(v.Response),
			ResponseCodingTable: responseTable,
			Transitional:        v.IsTransitional(),
			Result:              result,
		}.build()
		methods = append(methods, method)
		if size := method.ResponseSize + method.ResponseMaxOutOfLine; size > maxResponseSize {
			maxResponseSize = size
		}
	}

	fuzzingName := strings.ReplaceAll(strings.ReplaceAll(string(val.Name), ".", "_"), "/", "_")

	r := protocolInner{
		Attributes:          val.Attributes,
		DeclName:            protocolName,
		ClassName:           protocolName.AppendName("_clazz").Natural.Name(),
		ServiceName:         val.GetServiceName(),
		ProxyName:           protocolName.AppendName("_Proxy").Natural,
		StubName:            protocolName.AppendName("_Stub").Natural,
		EventSenderName:     protocolName.AppendName("_EventSender").Natural,
		SyncName:            protocolName.AppendName("_Sync").Natural,
		SyncProxyName:       protocolName.AppendName("_SyncProxy").Natural,
		RequestEncoderName:  protocolName.AppendName("_RequestEncoder").Natural,
		RequestDecoderName:  protocolName.AppendName("_RequestDecoder").Natural,
		ResponseEncoderName: protocolName.AppendName("_ResponseEncoder").Natural,
		ResponseDecoderName: protocolName.AppendName("_ResponseDecoder").Natural,
		ByteBufferType:      byteBufferType(maxResponseSize, 0, boundednessBounded),
		Methods:             methods,
		FuzzingName:         fuzzingName,
		TestBase:            protocolName.AppendName("_TestBase").AppendNamespace("testing"),
	}.build()
	return r
}

func (c *compiler) compileService(val fidl.Service) Service {
	s := Service{
		Attributes:  val.Attributes,
		DeclName:    c.compileDeclName(val.Name),
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		s.Members = append(s.Members, c.compileServiceMember(v))
	}
	return s
}

func (c *compiler) compileServiceMember(val fidl.ServiceMember) ServiceMember {
	return ServiceMember{
		Attributes:   val.Attributes,
		ProtocolType: c.compileDeclName(val.Type.Identifier),
		Name:         string(val.Name),
		MethodName:   changeIfReserved(val.Name),
	}
}

func (c *compiler) compileTableMember(val fidl.TableMember, index int) TableMember {
	t := c.compileType(val.Type)

	defaultValue := ConstantValue{}
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type)
	}

	return TableMember{
		Attributes:         val.Attributes,
		Type:               t,
		Name:               changeIfReserved(val.Name),
		DefaultValue:       defaultValue,
		Ordinal:            val.Ordinal,
		FieldPresenceIsSet: fmt.Sprintf("field_presence_.IsSet<%d>()", val.Ordinal-1),
		FieldPresenceSet:   fmt.Sprintf("field_presence_.Set<%d>()", val.Ordinal-1),
		FieldPresenceClear: fmt.Sprintf("field_presence_.Clear<%d>()", val.Ordinal-1),
		FieldDataName:      fmt.Sprintf("%s_value_", val.Name),
		MethodHasName:      fmt.Sprintf("has_%s", val.Name),
		MethodClearName:    fmt.Sprintf("clear_%s", val.Name),
		ValueUnionName:     fmt.Sprintf("ValueUnion_%s", val.Name),
		HandleInformation:  c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileTable(val fidl.Table) Table {
	name := c.compileDeclName(val.Name)
	tableType := c.compileTableType(val.Name)
	r := Table{
		Attributes:     val.Attributes,
		Resourceness:   val.Resourceness,
		DeclName:       name,
		TableType:      tableType,
		Members:        nil,
		InlineSize:     val.TypeShapeV1.InlineSize,
		BiggestOrdinal: 0,
		MaxHandles:     val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:   val.TypeShapeV1.MaxOutOfLine,
		ByteBufferType: byteBufferType(val.TypeShapeV1.InlineSize, val.TypeShapeV1.MaxOutOfLine, boundednessBounded),
		HasPointer:     val.TypeShapeV1.Depth > 0,
	}

	for i, v := range val.SortedMembersNoReserved() {
		m := c.compileTableMember(v, i)
		if m.Ordinal > r.BiggestOrdinal {
			r.BiggestOrdinal = m.Ordinal
		}
		r.Members = append(r.Members, m)
	}

	r.FrameItems = make([]TableFrameItem, r.BiggestOrdinal)
	for index, member := range r.Members {
		r.FrameItems[member.Ordinal-1] = &r.Members[index]
	}

	return r
}

func (c *compiler) compileUnionMember(val fidl.UnionMember) UnionMember {
	n := changeIfReserved(val.Name)
	return UnionMember{
		Attributes:        val.Attributes,
		Ordinal:           uint64(val.Ordinal),
		Type:              c.compileType(val.Type),
		Name:              n,
		StorageName:       changeIfReserved(val.Name + "_"),
		TagName:           fmt.Sprintf("k%s", fidl.ToUpperCamelCase(n)),
		Offset:            val.Offset,
		HandleInformation: c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileUnion(val fidl.Union) Union {
	name := c.compileDeclName(val.Name)
	tableType := c.compileTableType(val.Name)
	r := Union{
		Attributes:   val.Attributes,
		Strictness:   val.Strictness,
		Resourceness: val.Resourceness,
		DeclName:     name,
		TableType:    tableType,
		InlineSize:   val.TypeShapeV1.InlineSize,
		MaxHandles:   val.TypeShapeV1.MaxHandles,
		MaxOutOfLine: val.TypeShapeV1.MaxOutOfLine,
		HasPointer:   val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	if val.Attributes.HasAttribute("Result") {
		if len(r.Members) != 2 {
			panic(fmt.Sprintf("result union must have two members: %v", val.Name))
		}
		if val.Members[0].Type.Kind != fidl.IdentifierType {
			panic(fmt.Sprintf("value member of result union must be an identifier: %v", val.Name))
		}
		valueStructDeclInfo, ok := c.decls[val.Members[0].Type.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Members[0].Type.Identifier))
		}
		if valueStructDeclInfo.Type != "struct" {
			panic(fmt.Sprintf("first member of result union not a struct: %v", val.Name))
		}
		result := Result{
			ResultDecl:      r.DeclName,
			ValueStructDecl: r.Members[0].Type.TypeName,
			ErrorDecl:       r.Members[1].Type.TypeName,
		}
		c.resultForStruct[val.Members[0].Type.Identifier] = &result
		c.resultForUnion[val.Name] = &result
		r.Result = &result
	}

	return r
}

func compile(r fidl.Root, commonNsFormatter libraryNamespaceFunc) Root {
	root := Root{}
	library := make(fidl.LibraryIdentifier, 0)
	rawLibrary := make(fidl.LibraryIdentifier, 0)
	for _, identifier := range fidl.ParseLibraryName(r.Name) {
		safeName := changeIfReserved(identifier)
		library = append(library, fidl.Identifier(safeName))
		rawLibrary = append(rawLibrary, identifier)
	}
	c := compiler{
		symbolPrefix:     formatLibraryPrefix(rawLibrary),
		decls:            r.DeclsWithDependencies(),
		library:          fidl.ParseLibraryName(r.Name),
		handleTypes:      make(map[fidl.HandleSubtype]struct{}),
		naturalNamespace: naturalLibraryNamespace,
		wireNamespace:    wireLibraryNamespace,
		commonNamespace:  commonNsFormatter,
		resultForStruct:  make(map[fidl.EncodedCompoundIdentifier]*Result),
		resultForUnion:   make(map[fidl.EncodedCompoundIdentifier]*Result),
	}

	root.Library = library
	root.LibraryCommonNamespace = c.commonNamespace(fidl.ParseLibraryName(r.Name))
	libraryReversed := make(fidl.LibraryIdentifier, len(library))
	for i, j := 0, len(library)-1; i < len(library); i, j = i+1, j-1 {
		libraryReversed[i] = library[j]
	}
	for i, identifier := range library {
		libraryReversed[len(libraryReversed)-i-1] = identifier
	}
	root.LibraryReversed = libraryReversed

	decls := make(map[fidl.EncodedCompoundIdentifier]Decl)

	for _, v := range r.Bits {
		d := c.compileBits(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Consts {
		d := c.compileConst(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Enums {
		d := c.compileEnum(v)
		decls[v.Name] = &d
	}

	// Note: for Result calculation unions must be compiled before structs.
	for _, v := range r.Unions {
		d := c.compileUnion(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Structs {
		// TODO(fxbug.dev/7704) remove once anonymous structs are supported
		if v.Anonymous {
			continue
		}
		d := c.compileStruct(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Tables {
		d := c.compileTable(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Protocols {
		d := c.compileProtocol(v)
		decls[v.Name] = d
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
		libraryIdent := fidl.ParseLibraryName(l.Name)
		root.Headers = append(root.Headers, formatLibraryPath(libraryIdent))
		root.FuzzerHeaders = append(root.FuzzerHeaders, formatLibraryPath(libraryIdent))
	}

	// zx::channel is always referenced by the protocols in llcpp bindings API
	if len(r.Protocols) > 0 {
		c.handleTypes["channel"] = struct{}{}
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

func CompileHL(r fidl.Root) Root {
	return compile(r.ForBindings("hlcpp"), hlLibraryNamespace)
}

func CompileLL(r fidl.Root) Root {
	return compile(r.ForBindings("llcpp"), commonLibraryNamespace)
}

func CompileLibFuzzer(r fidl.Root) Root {
	return compile(r.ForBindings("libfuzzer"), hlLibraryNamespace)
}
