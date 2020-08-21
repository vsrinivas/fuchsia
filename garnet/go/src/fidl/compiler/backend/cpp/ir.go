// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"fmt"
	"log"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

// This value needs to be kept in sync with the one defined in
// zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
const llcppMaxStackAllocSize = 512

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

type Decl interface{}

type FamilyKind string

const (
	// TrivialCopy identifies values for whom a copy is trivial (like integers)
	TrivialCopy FamilyKind = "TrivialCopy"
	// Reference identifies values with a non trivial copy for which we use a reference on the
	// caller argument.
	Reference FamilyKind = "Reference"
	// String identifies string values for which we can use a const reference and for which we can
	// optimize the field construction.
	String FamilyKind = "String"
	// Vector identifies vector values for which we can use a reference and for which we can
	// optimize the field construction.
	Vector FamilyKind = "Vector"
)

type Type struct {
	// Use Type.Decl when you want to _declare_ a class/struct, e.g. "class Foo { … }". If you need
	// to reference a class by its name (e.g. "new Foo"), use the Type.Identifier() method instead.
	// Identifier() will add a type qualifier to the class name so that the compiler will resolve
	// the class, even if any locally non-type declarations are present (e.g. "enum Foo"). Google
	// for "C++ elaborated type specifier" for more details.
	Decl     string
	FullDecl string // Decl but with full type name
	LLDecl   string

	// Defines what operation we should use to pass a value without a move (LLCPP). It also
	// defines the way we should initialize a field.
	LLFamily FamilyKind

	// NeedsDtor indicates whether this type needs to be destructed explicitely
	// or not.
	NeedsDtor bool

	DeclType types.DeclType

	IsPrimitive bool
	IsArray     bool
	IsVector    bool
	IsTable     bool

	// Set iff IsArray || IsVector
	ElementType *Type
	// Valid iff IsArray
	ElementCount int
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

type Union struct {
	types.Attributes
	Namespace    string
	Name         string
	TableType    string
	Members      []UnionMember
	InlineSize   int
	MaxHandles   int
	MaxOutOfLine int
	Result       *Result
	HasPointer   bool
	IsResource   bool
	Kind         unionKind
	types.Strictness
}

type UnionMember struct {
	types.Attributes
	Ordinal     uint64
	Type        Type
	Name        string
	StorageName string
	TagName     string
	Offset      int
}

func (um UnionMember) UpperCamelCaseName() string {
	return common.ToUpperCamelCase(um.Name)
}

type TableFrameItem struct {
	LLDecl string // "void" if reserved
	Name   string
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
	IsResource     bool
	Kind           tableKind
	// Types of the members in ordinal order, "void" for reserved.
	FrameItems []TableFrameItem
}

type TableMember struct {
	types.Attributes
	Type               Type
	Name               string
	DefaultValue       string
	Ordinal            int
	FieldPresenceIsSet string
	FieldPresenceSet   string
	FieldPresenceClear string
	FieldDataName      string
	MethodHasName      string
	MethodClearName    string
	ValueUnionName     string
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
	IsResource    bool
	Result        *Result
	Kind          structKind
	// Full decls needed to check if a type is memcpy compatible.
	// Only set if it may be possible for a type to be memcpy compatible,
	// e.g. has no padding.
	// See the struct template for usage.
	FullDeclMemcpyCompatibleDeps []string
}

type StructMember struct {
	types.Attributes
	Type         Type
	Name         string
	DefaultValue string
	Offset       int
}

type Protocol struct {
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
	Kind                protocolKind
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
	ProtocolType string
	Name         string
	MethodName   string
}

// TODO: There are common fields between Request and Response; consider factoring them out.
type Method struct {
	types.Attributes
	Name                 string
	NameInLowerSnakeCase string
	Ordinal              uint64
	// The name of a constant that defines the ordinal value.
	OrdinalName             string
	HasRequest              bool
	Request                 []Parameter
	RequestSize             int
	RequestTypeName         string
	RequestMaxHandles       int
	RequestMaxOutOfLine     int
	RequestSentMaxSize      int
	RequestPadding          bool
	RequestFlexible         bool
	RequestHasPointer       bool
	RequestIsResource       bool
	HasResponse             bool
	Response                []Parameter
	ResponseSize            int
	ResponseTypeName        string
	ResponseMaxHandles      int
	ResponseMaxOutOfLine    int
	ResponseSentMaxSize     int
	ResponseReceivedMaxSize int
	ResponsePadding         bool
	ResponseFlexible        bool
	ResponseHasPointer      bool
	ResponseIsResource      bool
	CallbackType            string
	ResponseHandlerType     string
	ResponderType           string
	Transitional            bool
	Result                  *Result
	LLProps                 LLProps
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
	ProtocolName      string
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

var reservedWords = map[string]struct{}{
	"alignas":          {},
	"alignof":          {},
	"and":              {},
	"and_eq":           {},
	"asm":              {},
	"assert":           {},
	"atomic_cancel":    {},
	"atomic_commit":    {},
	"atomic_noexcept":  {},
	"auto":             {},
	"bitand":           {},
	"bitor":            {},
	"bool":             {},
	"break":            {},
	"case":             {},
	"catch":            {},
	"char":             {},
	"char16_t":         {},
	"char32_t":         {},
	"class":            {},
	"compl":            {},
	"concept":          {},
	"const":            {},
	"constexpr":        {},
	"const_cast":       {},
	"continue":         {},
	"co_await":         {},
	"co_return":        {},
	"co_yield":         {},
	"decltype":         {},
	"default":          {},
	"delete":           {},
	"do":               {},
	"double":           {},
	"dynamic_cast":     {},
	"else":             {},
	"enum":             {},
	"explicit":         {},
	"export":           {},
	"extern":           {},
	"false":            {},
	"float":            {},
	"for":              {},
	"friend":           {},
	"goto":             {},
	"if":               {},
	"import":           {},
	"inline":           {},
	"int":              {},
	"long":             {},
	"module":           {},
	"mutable":          {},
	"namespace":        {},
	"new":              {},
	"noexcept":         {},
	"not":              {},
	"not_eq":           {},
	"NULL":             {},
	"nullptr":          {},
	"offsetof":         {},
	"operator":         {},
	"or":               {},
	"or_eq":            {},
	"private":          {},
	"protected":        {},
	"public":           {},
	"register":         {},
	"reinterpret_cast": {},
	"requires":         {},
	"return":           {},
	"short":            {},
	"signed":           {},
	"sizeof":           {},
	"static":           {},
	"static_assert":    {},
	"static_cast":      {},
	"struct":           {},
	"switch":           {},
	"synchronized":     {},
	"template":         {},
	"this":             {},
	"thread_local":     {},
	"throw":            {},
	"true":             {},
	"try":              {},
	"typedef":          {},
	"typeid":           {},
	"typename":         {},
	"union":            {},
	"unsigned":         {},
	"using":            {},
	"virtual":          {},
	"void":             {},
	"volatile":         {},
	"wchar_t":          {},
	"while":            {},
	"xor":              {},
	"xor_eq":           {},
	"xunion":           {},

	// names used in specific contexts e.g. union accessors
	"FidlType":        {},
	"New":             {},
	"Tag":             {},
	"Which":           {},
	"has_invalid_tag": {},
	"which":           {},
	"Unknown":         {},
	"unknown":         {},
	"IsEmpty":         {},
	"HandleEvents":    {},
	// TODO(ianloic) add: "Clone"
	// There are Clone methods on a couple of protocols that are used
	// across layers so this will be a breaking change.
	// FIDL-461

	// All names from errno definitions.
	"EPERM":           {},
	"ENOENT":          {},
	"ESRCH":           {},
	"EINTR":           {},
	"EIO":             {},
	"ENXIO":           {},
	"E2BIG":           {},
	"ENOEXEC":         {},
	"EBADF":           {},
	"ECHILD":          {},
	"EAGAIN":          {},
	"ENOMEM":          {},
	"EACCES":          {},
	"EFAULT":          {},
	"ENOTBLK":         {},
	"EBUSY":           {},
	"EEXIST":          {},
	"EXDEV":           {},
	"ENODEV":          {},
	"ENOTDIR":         {},
	"EISDIR":          {},
	"EINVAL":          {},
	"ENFILE":          {},
	"EMFILE":          {},
	"ENOTTY":          {},
	"ETXTBSY":         {},
	"EFBIG":           {},
	"ENOSPC":          {},
	"ESPIPE":          {},
	"EROFS":           {},
	"EMLINK":          {},
	"EPIPE":           {},
	"EDOM":            {},
	"ERANGE":          {},
	"EDEADLK":         {},
	"ENAMETOOLONG":    {},
	"ENOLCK":          {},
	"ENOSYS":          {},
	"ENOTEMPTY":       {},
	"ELOOP":           {},
	"EWOULDBLOCK":     {},
	"ENOMSG":          {},
	"EIDRM":           {},
	"ECHRNG":          {},
	"EL2NSYNC":        {},
	"EL3HLT":          {},
	"EL3RST":          {},
	"ELNRNG":          {},
	"EUNATCH":         {},
	"ENOCSI":          {},
	"EL2HLT":          {},
	"EBADE":           {},
	"EBADR":           {},
	"EXFULL":          {},
	"ENOANO":          {},
	"EBADRQC":         {},
	"EBADSLT":         {},
	"EDEADLOCK":       {},
	"EBFONT":          {},
	"ENOSTR":          {},
	"ENODATA":         {},
	"ETIME":           {},
	"ENOSR":           {},
	"ENONET":          {},
	"ENOPKG":          {},
	"EREMOTE":         {},
	"ENOLINK":         {},
	"EADV":            {},
	"ESRMNT":          {},
	"ECOMM":           {},
	"EPROTO":          {},
	"EMULTIHOP":       {},
	"EDOTDOT":         {},
	"EBADMSG":         {},
	"EOVERFLOW":       {},
	"ENOTUNIQ":        {},
	"EBADFD":          {},
	"EREMCHG":         {},
	"ELIBACC":         {},
	"ELIBBAD":         {},
	"ELIBSCN":         {},
	"ELIBMAX":         {},
	"ELIBEXEC":        {},
	"EILSEQ":          {},
	"ERESTART":        {},
	"ESTRPIPE":        {},
	"EUSERS":          {},
	"ENOTSOCK":        {},
	"EDESTADDRREQ":    {},
	"EMSGSIZE":        {},
	"EPROTOTYPE":      {},
	"ENOPROTOOPT":     {},
	"EPROTONOSUPPORT": {},
	"ESOCKTNOSUPPORT": {},
	"EOPNOTSUPP":      {},
	"ENOTSUP":         {},
	"EPFNOSUPPORT":    {},
	"EAFNOSUPPORT":    {},
	"EADDRINUSE":      {},
	"EADDRNOTAVAIL":   {},
	"ENETDOWN":        {},
	"ENETUNREACH":     {},
	"ENETRESET":       {},
	"ECONNABORTED":    {},
	"ECONNRESET":      {},
	"ENOBUFS":         {},
	"EISCONN":         {},
	"ENOTCONN":        {},
	"ESHUTDOWN":       {},
	"ETOOMANYREFS":    {},
	"ETIMEDOUT":       {},
	"ECONNREFUSED":    {},
	"EHOSTDOWN":       {},
	"EHOSTUNREACH":    {},
	"EALREADY":        {},
	"EINPROGRESS":     {},
	"ESTALE":          {},
	"EUCLEAN":         {},
	"ENOTNAM":         {},
	"ENAVAIL":         {},
	"EISNAM":          {},
	"EREMOTEIO":       {},
	"EDQUOT":          {},
	"ENOMEDIUM":       {},
	"EMEDIUMTYPE":     {},
	"ECANCELED":       {},
	"ENOKEY":          {},
	"EKEYEXPIRED":     {},
	"EKEYREVOKED":     {},
	"EKEYREJECTED":    {},
	"EOWNERDEAD":      {},
	"ENOTRECOVERABLE": {},
	"ERFKILL":         {},
	"EHWPOISON":       {},
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

type compiler struct {
	namespace          string
	symbolPrefix       string
	decls              types.DeclMap
	library            types.LibraryIdentifier
	handleTypes        map[types.HandleSubtype]struct{}
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
	if len(val.Member) != 0 {
		strs = append(strs, changeIfReserved(val.Member, ext))
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
		r.FullDecl = fmt.Sprintf("::std::array<%s, %v>", t.FullDecl, *val.ElementCount)
		r.LLDecl = fmt.Sprintf("::fidl::Array<%s, %v>", t.LLDecl, *val.ElementCount)
		r.LLFamily = Reference
		r.NeedsDtor = true
		r.IsArray = true
		r.ElementType = &t
		r.ElementCount = *val.ElementCount
	case types.VectorType:
		t := c.compileType(*val.ElementType)
		r.LLDecl = fmt.Sprintf("::fidl::VectorView<%s>", t.LLDecl)
		r.LLFamily = Vector
		if val.Nullable {
			r.Decl = fmt.Sprintf("::fidl::VectorPtr<%s>", t.Decl)
			r.FullDecl = fmt.Sprintf("::fidl::VectorPtr<%s>", t.FullDecl)
		} else {
			r.Decl = fmt.Sprintf("::std::vector<%s>", t.Decl)
			r.FullDecl = fmt.Sprintf("::std::vector<%s>", t.FullDecl)
		}
		r.NeedsDtor = true
		r.IsVector = true
		r.ElementType = &t
	case types.StringType:
		r.LLDecl = "::fidl::StringView"
		r.LLFamily = String
		if val.Nullable {
			r.Decl = "::fidl::StringPtr"
		} else {
			r.Decl = "::std::string"
		}
		r.FullDecl = r.Decl
		r.NeedsDtor = true
	case types.HandleType:
		c.handleTypes[val.HandleSubtype] = struct{}{}
		r.Decl = fmt.Sprintf("::zx::%s", val.HandleSubtype)
		r.FullDecl = r.Decl
		r.LLDecl = r.Decl
		r.LLFamily = Reference
		r.NeedsDtor = true
	case types.RequestType:
		r.Decl = fmt.Sprintf("::fidl::InterfaceRequest<%s>",
			c.compileCompoundIdentifier(val.RequestSubtype, "", "", false))
		r.FullDecl = fmt.Sprintf("::fidl::InterfaceRequest<%s>",
			c.compileCompoundIdentifier(val.RequestSubtype, "", "", true))
		r.LLDecl = "::zx::channel"
		r.LLFamily = Reference
		r.NeedsDtor = true
	case types.PrimitiveType:
		r.Decl = c.compilePrimitiveSubtype(val.PrimitiveSubtype)
		r.FullDecl = r.Decl
		r.LLDecl = r.Decl
		r.LLFamily = TrivialCopy
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
			if declType.IsPrimitive() {
				r.IsPrimitive = true
			}
			if declType == types.TableDeclType {
				r.IsTable = true
			}

			if val.Nullable {
				r.Decl = fmt.Sprintf("::std::unique_ptr<%s>", t)
				r.FullDecl = fmt.Sprintf("::std::unique_ptr<%s>", ft)
				if declType == types.UnionDeclType {
					r.LLDecl = fmt.Sprintf("%s", ft)
				} else {
					r.LLDecl = fmt.Sprintf("::fidl::tracking_ptr<%s>", ft)
				}
				r.NeedsDtor = true
			} else {
				r.Decl = t
				r.FullDecl = ft
				r.LLDecl = ft
				r.NeedsDtor = true
			}
			if declType.IsPrimitive() {
				r.LLFamily = TrivialCopy
			} else {
				r.LLFamily = Reference
			}
		case types.ProtocolDeclType:
			r.Decl = fmt.Sprintf("::fidl::InterfaceHandle<class %s>", t)
			r.FullDecl = fmt.Sprintf("::fidl::InterfaceHandle<class %s>", ft)
			r.LLDecl = "::zx::channel"
			r.LLFamily = Reference
			r.NeedsDtor = true
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

func (m Method) NewLLProps(r Protocol, reqTypeShape types.TypeShape, respTypeShape types.TypeShape) LLProps {
	return LLProps{
		ProtocolName:      r.Name,
		LinearizeRequest:  len(m.Request) > 0 && reqTypeShape.Depth > 0,
		LinearizeResponse: len(m.Response) > 0 && respTypeShape.Depth > 0,
		ClientContext:     m.NewLLContextProps(clientContext),
		ServerContext:     m.NewLLContextProps(serverContext),
	}
}

func (c *compiler) compileProtocol(val types.Protocol) Protocol {
	r := Protocol{
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

		var computedResponseReceivedMaxSize int
		if v.ResponseTypeShapeV1.HasFlexibleEnvelope {
			computedResponseReceivedMaxSize = (1 << 32) - 1
		} else {
			computedResponseReceivedMaxSize = v.ResponseTypeShapeV1.InlineSize + v.ResponseTypeShapeV1.MaxOutOfLine
		}

		m := Method{
			Attributes:              v.Attributes,
			Name:                    name,
			NameInLowerSnakeCase:    common.ToSnakeCase(name),
			Ordinal:                 v.Ordinal,
			OrdinalName:             fmt.Sprintf("k%s_%s_Ordinal", r.Name, v.Name),
			HasRequest:              v.HasRequest,
			Request:                 c.compileParameterArray(v.Request),
			RequestSize:             v.RequestTypeShapeV1.InlineSize,
			RequestTypeName:         fmt.Sprintf("%s_%s%sRequestTable", c.symbolPrefix, r.Name, v.Name),
			RequestMaxHandles:       v.RequestTypeShapeV1.MaxHandles,
			RequestMaxOutOfLine:     v.RequestTypeShapeV1.MaxOutOfLine,
			RequestSentMaxSize:      v.RequestTypeShapeV1.InlineSize + v.RequestTypeShapeV1.MaxOutOfLine,
			RequestPadding:          v.RequestTypeShapeV1.HasPadding,
			RequestFlexible:         v.RequestTypeShapeV1.HasFlexibleEnvelope,
			RequestHasPointer:       v.RequestTypeShapeV1.Depth > 0,
			RequestIsResource:       v.RequestTypeShapeV1.IsResource,
			HasResponse:             v.HasResponse,
			Response:                c.compileParameterArray(v.Response),
			ResponseSize:            v.ResponseTypeShapeV1.InlineSize,
			ResponseTypeName:        fmt.Sprintf("%s_%s%s%s", c.symbolPrefix, r.Name, v.Name, responseTypeNameSuffix),
			ResponseMaxHandles:      v.ResponseTypeShapeV1.MaxHandles,
			ResponseMaxOutOfLine:    v.ResponseTypeShapeV1.MaxOutOfLine,
			ResponseSentMaxSize:     v.ResponseTypeShapeV1.InlineSize + v.ResponseTypeShapeV1.MaxOutOfLine,
			ResponseReceivedMaxSize: computedResponseReceivedMaxSize,
			ResponsePadding:         v.ResponseTypeShapeV1.HasPadding,
			ResponseFlexible:        v.ResponseTypeShapeV1.HasFlexibleEnvelope,
			ResponseHasPointer:      v.ResponseTypeShapeV1.Depth > 0,
			ResponseIsResource:      v.ResponseTypeShapeV1.IsResource,
			CallbackType:            callbackType,
			ResponseHandlerType:     fmt.Sprintf("%s_%s_ResponseHandler", r.Name, v.Name),
			ResponderType:           fmt.Sprintf("%s_%s_Responder", r.Name, v.Name),
			Transitional:            v.IsTransitional(),
			Result:                  result,
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
		Attributes:   val.Attributes,
		ProtocolType: c.compileCompoundIdentifier(val.Type.Identifier, "", "", false),
		Name:         string(val.Name),
		MethodName:   changeIfReserved(val.Name, ""),
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
		TableType:    tableType,
		Members:      []StructMember{},
		InlineSize:   val.TypeShapeV1.InlineSize,
		MaxHandles:   val.TypeShapeV1.MaxHandles,
		MaxOutOfLine: val.TypeShapeV1.MaxOutOfLine,
		HasPadding:   val.TypeShapeV1.HasPadding,
		HasPointer:   val.TypeShapeV1.Depth > 0,
		IsResource:   val.TypeShapeV1.IsResource,
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

	// Construct a deduped list of decls for IsMemcpyCompatible template definitions.
	memcpyCompatibleDepMap := make(map[string]struct{})
	for _, member := range r.Members {
		// The dangerous identifiers test package contains identifiers that won't compile.
		// e.g. ::fidl::test::dangerous::struct::types::camel::Interface gives an
		// "expected unqualified-id" error because of "struct".
		// There isn't an easily accessible dangerous identifiers list to replace identifiers.
		if strings.Contains(member.Type.FullDecl, "::fidl::test::dangerous::") {
			memcpyCompatibleDepMap = nil
			break
		}
		memcpyCompatibleDepMap[member.Type.FullDecl] = struct{}{}
	}
	for decl := range memcpyCompatibleDepMap {
		r.FullDeclMemcpyCompatibleDeps = append(r.FullDeclMemcpyCompatibleDeps, decl)
	}
	sort.Strings(r.FullDeclMemcpyCompatibleDeps)

	return r
}

func (c *compiler) compileTableMember(val types.TableMember, appendNamespace string, index int) TableMember {
	t := c.compileType(val.Type)

	defaultValue := ""
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type, appendNamespace)
	}

	return TableMember{
		Attributes:         val.Attributes,
		Type:               t,
		Name:               changeIfReserved(val.Name, ""),
		DefaultValue:       defaultValue,
		Ordinal:            val.Ordinal,
		FieldPresenceIsSet: fmt.Sprintf("field_presence_.IsSet<%d>()", val.Ordinal-1),
		FieldPresenceSet:   fmt.Sprintf("field_presence_.Set<%d>()", val.Ordinal-1),
		FieldPresenceClear: fmt.Sprintf("field_presence_.Clear<%d>()", val.Ordinal-1),
		FieldDataName:      fmt.Sprintf("%s_value_", val.Name),
		MethodHasName:      fmt.Sprintf("has_%s", val.Name),
		MethodClearName:    fmt.Sprintf("clear_%s", val.Name),
		ValueUnionName:     fmt.Sprintf("ValueUnion_%s", val.Name),
	}
}

func (c *compiler) compileTable(val types.Table, appendNamespace string) Table {
	name := c.compileCompoundIdentifier(val.Name, "", appendNamespace, false)
	tableType := c.compileTableType(val.Name)
	r := Table{
		Attributes:     val.Attributes,
		Namespace:      c.namespace,
		Name:           name,
		TableType:      tableType,
		Members:        nil,
		InlineSize:     val.TypeShapeV1.InlineSize,
		BiggestOrdinal: 0,
		MaxHandles:     val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:   val.TypeShapeV1.MaxOutOfLine,
		HasPointer:     val.TypeShapeV1.Depth > 0,
		IsResource:     val.TypeShapeV1.IsResource,
	}

	for i, v := range val.SortedMembersNoReserved() {
		m := c.compileTableMember(v, appendNamespace, i)
		if m.Ordinal > r.BiggestOrdinal {
			r.BiggestOrdinal = m.Ordinal
		}
		r.Members = append(r.Members, m)
	}

	r.FrameItems = make([]TableFrameItem, r.BiggestOrdinal)
	for i := 0; i < len(r.FrameItems); i++ {
		r.FrameItems[i] = TableFrameItem{
			LLDecl: "void",
			Name:   fmt.Sprintf("reserved_%d", i),
		}
	}
	for _, member := range r.Members {
		r.FrameItems[member.Ordinal-1] = TableFrameItem{
			LLDecl: member.Type.LLDecl,
			Name:   member.Name,
		}
	}

	return r
}

func (c *compiler) compileUnionMember(val types.UnionMember) UnionMember {
	n := changeIfReserved(val.Name, "")
	return UnionMember{
		Attributes:  val.Attributes,
		Ordinal:     uint64(val.Ordinal),
		Type:        c.compileType(val.Type),
		Name:        n,
		StorageName: changeIfReserved(val.Name, "_"),
		TagName:     fmt.Sprintf("k%s", common.ToUpperCamelCase(n)),
		Offset:      val.Offset,
	}
}

func (c *compiler) compileUnion(val types.Union) Union {
	name := c.compileCompoundIdentifier(val.Name, "", "", false)
	tableType := c.compileTableType(val.Name)
	r := Union{
		Attributes:   val.Attributes,
		Namespace:    c.namespace,
		Name:         name,
		TableType:    tableType,
		InlineSize:   val.TypeShapeV1.InlineSize,
		MaxHandles:   val.TypeShapeV1.MaxHandles,
		MaxOutOfLine: val.TypeShapeV1.MaxOutOfLine,
		Strictness:   val.Strictness,
		HasPointer:   val.TypeShapeV1.Depth > 0,
		IsResource:   val.TypeShapeV1.IsResource,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
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
		make(map[types.HandleSubtype]struct{}),
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
		d := c.compileUnion(v)
		decls[v.Name] = d
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

	for _, v := range r.Protocols {
		d := c.compileProtocol(v)
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

func CompileHL(r types.Root) Root {
	return compile(r.ForBindings("cpp"), formatNamespace)
}

func CompileLL(r types.Root) Root {
	return compile(r.ForBindings("llcpp"), formatLLNamespace)
}

func CompileLibFuzzer(r types.Root) Root {
	return compile(r.ForBindings("libfuzzer"), formatNamespace)
}
