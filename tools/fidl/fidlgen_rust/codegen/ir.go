// Copyright 2018 The Fuchsia Authors. All rights reserved.  Use of this source
// code is governed by a BSD-style license that can be found in the LICENSE
// file.

package codegen

import (
	"encoding/binary"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"unicode"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type EncodedCompoundIdentifier = fidlgen.EncodedCompoundIdentifier

type Bits struct {
	fidlgen.Bits
	Name    string
	Type    string
	Members []BitsMember
}

type BitsMember struct {
	fidlgen.BitsMember
	Name  string
	Value string
}

type Const struct {
	fidlgen.Const
	Name  string
	Type  string
	Value string
}

type Enum struct {
	fidlgen.Enum
	Name    string
	Type    string
	Members []EnumMember
	// Member name with the minimum value, used as an arbitrary default value
	// in Decodable::new_empty for strict enums.
	MinMember string
}

type EnumMember struct {
	fidlgen.EnumMember
	Name  string
	Value string
}

type Union struct {
	fidlgen.Union
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []UnionMember
}

type UnionMember struct {
	fidlgen.UnionMember
	Type              string
	OGType            fidlgen.Type
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

type ResultOkEntry struct {
	OGType            fidlgen.Type
	Type              string
	HasHandleMetadata bool
	HandleWrapperName string
}

// A Result is the result type used for a method that is flexible or uses error syntax.
type Result struct {
	// Compound identifier for the result type, used for lookups.
	ECI     EncodedCompoundIdentifier
	Derives derives
	// Rust UpperCamelCase name for the result type used when generating or
	// referencing it.
	Name              string
	Ok                []ResultOkEntry
	ErrOGType         *fidlgen.Type
	ErrType           *string
	HasTransportError bool
}

// HasAnyHandleWrappers returns true if any value in Ok uses a handle wrapper.
func (r *Result) HasAnyHandleWrappers() bool {
	for _, okEntry := range r.Ok {
		if okEntry.HasHandleMetadata {
			return true
		}
	}
	return false
}

type Struct struct {
	fidlgen.Struct
	ECI                                                  EncodedCompoundIdentifier
	Derives                                              derives
	Name                                                 string
	Members                                              []StructMember
	PaddingMarkersV1, PaddingMarkersV2                   []rustPaddingMarker
	FlattenedPaddingMarkersV1, FlattenedPaddingMarkersV2 []rustPaddingMarker
	SizeV1, SizeV2                                       int
	AlignmentV1, AlignmentV2                             int
	HasPadding                                           bool
	// True iff the fidl_struct_copy! macro should be used instead of fidl_struct!.
	UseFidlStructCopy bool
}

type StructMember struct {
	fidlgen.StructMember
	OGType             fidlgen.Type
	Type               string
	Name               string
	OffsetV1, OffsetV2 int
	HasDefault         bool
	DefaultValue       string
	HasHandleMetadata  bool
	HandleRights       string
	HandleSubtype      string
}

type Table struct {
	fidlgen.Table
	Derives derives
	ECI     EncodedCompoundIdentifier
	Name    string
	Members []TableMember
}

type TableMember struct {
	fidlgen.TableMember
	OGType            fidlgen.Type
	Type              string
	Name              string
	Ordinal           int
	HasHandleMetadata bool
	HandleRights      string
	HandleSubtype     string
}

// Protocol is the definition of a protocol in the library being compiled.
type Protocol struct {
	// Raw JSON IR data about this protocol. Embedded to provide access to
	// fields common to all bindings.
	fidlgen.Protocol
	// Compound identifier referring to this protocol.
	ECI EncodedCompoundIdentifier
	// Name of the protocol as a Rust CamelCase identifier. Since only protocols
	// from the same library are included, this will never be qualified, so it
	// is just the CamelCase name of the protocol.
	Name string
	// List of methods that are part of this protocol. Processed from
	// fidlgen.Protocol to add Rust-specific fields.
	Methods []Method
	// Name of this protocol for legacy (pre-RFC-0041) service discovery, if the
	// protocol is marked as discoverable. This value does not include enclosing
	// quote marks.
	ProtocolName string
}

// Overflowing stores information about a method's payloads, indicating whether
// it is possible for either of them to overflow on either encode or decode.
// TODO(fxbug.dev/106641): this information will be included in the IR in the
// final implementation. For the current prototype, inferring it from a user
// supplied (but allowlist guarded) `@experimental_overflowing` attribute is
// sufficient.
type Overflowing struct {
	// OnRequestEncode indicates whether or not the parent method's request
	// payload may be so large on encode as to require overflow handling.
	OnRequestEncode bool
	// OnRequestDecode indicates whether or not the parent method's request
	// payload may be so large on decode as to require overflow handling. This
	// will always be true if OnRequestEncode is true, as the maximum size on
	// decode is always larger than encode. This is because only the latter may
	// include unknown, arbitrarily large data.
	OnRequestDecode bool
	// OnResponseEncode indicates whether or not the parent method's response
	// payload may be so large on encode as to require overflow handling.
	OnResponseEncode bool
	// OnResponseDecode indicates whether or not the parent method's response
	// payload may be so large on decode as to require overflow handling. This
	// will always be true if OnResponseEncode is true, as the maximum size on
	// decode is always larger than encode. This is because only the latter may
	// include unknown, arbitrarily large data.
	OnResponseDecode bool
}

// Method is a method defined in a protocol.
type Method struct {
	// Raw JSON IR data about this method. Embedded to provide access to fields
	// common to all bindings.
	fidlgen.Method
	// Name of the method converted to snake_case. Used when generating
	// rust-methods associated with this method, such as proxy methods and
	// encoder methods.
	Name string
	// Name of the method converted to CamelCase. Used when generating
	// rust-types associated with this method, such as responders.
	CamelName string
	// Parameters to this method extracted from the request type struct.
	Request []Parameter
	// Arguments used for method responses. If error syntax is used, this will
	// contain a single element for the Result enum used in rust generated code.
	// For methods which do not use error syntax, this will contain fields
	// extracted from the response struct.
	//
	// Note that since methods being strict vs flexible is not exposed in the
	// client API, this field does not reflect whether the method is strict or
	// flexible. For flexible, the value is still either fields extracted from
	// the response struct or the Rust Result enum, depending only on whether
	// error syntax was used.  In the case of flexible methods without error
	// syntax, the parameters are extracted from the success variant of the
	// underlying result union.
	Response []Parameter
	// If error syntax was used, this will contain information about the result
	// union.
	Result *Result
	// Stores overflowing information for this method's payloads.
	Overflowing Overflowing
}

// DynamicFlags gets rust code for the DynamicFlags value that should be set for
// a call to this method.
func (m *Method) DynamicFlags() string {
	if m.IsStrict() {
		return "fidl::encoding::DynamicFlags::empty()"
	}
	return "fidl::encoding::DynamicFlags::FLEXIBLE"
}

// HasErrorResult returns true if the method uses error syntax.
func (m *Method) HasErrorResult() bool {
	return m.Result != nil && m.Result.ErrOGType != nil
}

// A Parameter to either the requset or response of a method. Contains
// information to assist in generating code using borrowed types and handle
// wrappers.
type Parameter struct {
	// The raw fidlgen type of the parameter.
	OGType fidlgen.Type
	// String representing the type to use for this parameter when handling it
	// by-value.
	Type string
	// String representing the type to use for this parameter when receiving it
	// as a possibly-borrowed method argument.
	BorrowedType string
	// Snake-case name to use for the parameter.
	Name string
	// Name of the wrapper type that should be used for handle validation, if
	// HasHandleMetadata is true.
	HandleWrapperName string
	// True if the type of the parameter has handle metadata and so requires
	// validation.
	HasHandleMetadata bool
}

type HandleMetadataWrapper struct {
	Name    string
	Subtype string
	Rights  string
}

type Service struct {
	fidlgen.Service
	Name        string
	Members     []ServiceMember
	ServiceName string
}

type ServiceMember struct {
	fidlgen.ServiceMember
	ProtocolType      string
	Name              string
	CamelName         string
	SnakeName         string
	ProtocolTransport string
}

type Root struct {
	Experiments     fidlgen.Experiments
	ExternCrates    []string
	Bits            []Bits
	Consts          []Const
	Enums           []Enum
	Structs         []Struct
	ExternalStructs []Struct
	Unions          []Union
	// Result types for methods with error syntax.
	Results                []Result
	Tables                 []Table
	Protocols              []Protocol
	Services               []Service
	HandleMetadataWrappers []HandleMetadataWrapper
}

func (r *Root) findProtocol(eci EncodedCompoundIdentifier) *Protocol {
	for i := range r.Protocols {
		if r.Protocols[i].ECI == eci {
			return &r.Protocols[i]
		}
	}
	return nil
}

func (r *Root) findStruct(eci EncodedCompoundIdentifier, canBeExternal bool) *Struct {
	for i := range r.Structs {
		if r.Structs[i].ECI == eci {
			return &r.Structs[i]
		}
	}
	if canBeExternal {
		for i := range r.ExternalStructs {
			if r.ExternalStructs[i].ECI == eci {
				return &r.ExternalStructs[i]
			}
		}
	}
	return nil
}

func (r *Root) findTable(eci EncodedCompoundIdentifier) *Table {
	for i := range r.Tables {
		if r.Tables[i].ECI == eci {
			return &r.Tables[i]
		}
	}
	return nil
}

func (r *Root) findResult(eci EncodedCompoundIdentifier) *Result {
	for i := range r.Results {
		if r.Results[i].ECI == eci {
			return &r.Results[i]
		}
	}
	return nil
}

func (r *Root) findUnion(eci EncodedCompoundIdentifier) *Union {
	for i := range r.Unions {
		if r.Unions[i].ECI == eci {
			return &r.Unions[i]
		}
	}
	return nil
}

// ServicesForTransport returns services containing exclusively protocol
// members defined over the specified transport.
func (r Root) ServicesForTransport() func(string) []Service {
	return func(t string) []Service {
		var ss []Service
		for _, s := range r.Services {
			allOk := true
			for _, m := range s.Members {
				if m.ProtocolTransport != t {
					allOk = false
					break
				}
			}
			if allOk {
				ss = append(ss, s)
			}
		}
		return ss
	}
}

var reservedWords = map[string]struct{}{
	"as":       {},
	"box":      {},
	"break":    {},
	"const":    {},
	"continue": {},
	"crate":    {},
	"else":     {},
	"enum":     {},
	"extern":   {},
	"false":    {},
	"fn":       {},
	"for":      {},
	"if":       {},
	"impl":     {},
	"in":       {},
	"let":      {},
	"loop":     {},
	"match":    {},
	"mod":      {},
	"move":     {},
	"mut":      {},
	"pub":      {},
	"ref":      {},
	"return":   {},
	"self":     {},
	"Self":     {},
	"static":   {},
	"struct":   {},
	"super":    {},
	"trait":    {},
	"true":     {},
	"type":     {},
	"unsafe":   {},
	"use":      {},
	"where":    {},
	"while":    {},

	// Keywords reserved for future use (future-proofing...)
	"abstract": {},
	"alignof":  {},
	"await":    {},
	"become":   {},
	"do":       {},
	"final":    {},
	"macro":    {},
	"offsetof": {},
	"override": {},
	"priv":     {},
	"proc":     {},
	"pure":     {},
	"sizeof":   {},
	"typeof":   {},
	"unsized":  {},
	"virtual":  {},
	"yield":    {},

	// Weak keywords (special meaning in specific contexts)
	// These are ok in all contexts of FIDL names.
	//"default":	{},
	//"union":	{},

	// Things that are not keywords, but for which collisions would be very
	// unpleasant
	"Result":  {},
	"Ok":      {},
	"Err":     {},
	"Vec":     {},
	"Option":  {},
	"Some":    {},
	"None":    {},
	"Box":     {},
	"Future":  {},
	"Stream":  {},
	"Never":   {},
	"Send":    {},
	"fidl":    {},
	"futures": {},
	"zx":      {},
	"async":   {},
	"on_open": {},
	"OnOpen":  {},
	// TODO(fxbug.dev/66767): Remove "WaitForEvent".
	"wait_for_event": {},
	"WaitForEvent":   {},
}

var reservedSuffixes = []string{
	"Impl",
	"Marker",
	"Proxy",
	"ProxyProtocol",
	"ControlHandle",
	"Responder",
	"Server",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

// hasReservedSuffix checks if a string ends with a suffix commonly used by the
// bindings in generated types
func hasReservedSuffix(str string) bool {
	for _, suffix := range reservedSuffixes {
		if strings.HasSuffix(str, suffix) {
			return true
		}
	}
	return false
}

// changeIfReserved adds an underscore suffix to differentiate an identifier
// from a reserved name
//
// Reserved names include a variety of rust keywords, commonly used rust types
// like Result, Vec, and Future, and any name ending in a suffix used by the
// bindings to identify particular generated types like -Impl, -Marker, and
// -Proxy.
func changeIfReserved(val fidlgen.Identifier) string {
	str := string(val)
	if hasReservedSuffix(str) || isReservedWord(str) {
		return str + "_"
	}
	return str
}

var primitiveTypes = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Bool:    "bool",
	fidlgen.Int8:    "i8",
	fidlgen.Int16:   "i16",
	fidlgen.Int32:   "i32",
	fidlgen.Int64:   "i64",
	fidlgen.Uint8:   "u8",
	fidlgen.Uint16:  "u16",
	fidlgen.Uint32:  "u32",
	fidlgen.Uint64:  "u64",
	fidlgen.Float32: "f32",
	fidlgen.Float64: "f64",
}

var handleSubtypes = map[fidlgen.HandleSubtype]string{
	fidlgen.HandleSubtypeBti:          "Bti",
	fidlgen.HandleSubtypeChannel:      "Channel",
	fidlgen.HandleSubtypeClock:        "Clock",
	fidlgen.HandleSubtypeDebugLog:     "DebugLog",
	fidlgen.HandleSubtypeEvent:        "Event",
	fidlgen.HandleSubtypeEventpair:    "EventPair",
	fidlgen.HandleSubtypeException:    "Exception",
	fidlgen.HandleSubtypeFifo:         "Fifo",
	fidlgen.HandleSubtypeGuest:        "Guest",
	fidlgen.HandleSubtypeInterrupt:    "Interrupt",
	fidlgen.HandleSubtypeIommu:        "Iommu",
	fidlgen.HandleSubtypeJob:          "Job",
	fidlgen.HandleSubtypeMsi:          "Msi",
	fidlgen.HandleSubtypeNone:         "Handle",
	fidlgen.HandleSubtypePager:        "Pager",
	fidlgen.HandleSubtypePmt:          "Pmt",
	fidlgen.HandleSubtypePort:         "Port",
	fidlgen.HandleSubtypeProcess:      "Process",
	fidlgen.HandleSubtypeProfile:      "Profile",
	fidlgen.HandleSubtypeResource:     "Resource",
	fidlgen.HandleSubtypeSocket:       "Socket",
	fidlgen.HandleSubtypeStream:       "Stream",
	fidlgen.HandleSubtypeSuspendToken: "SuspendToken",
	fidlgen.HandleSubtypeThread:       "Thread",
	fidlgen.HandleSubtypeTime:         "Timer",
	fidlgen.HandleSubtypeVcpu:         "Vcpu",
	fidlgen.HandleSubtypeVmar:         "Vmar",
	fidlgen.HandleSubtypeVmo:          "Vmo",
}

var handleSubtypeConsts = map[fidlgen.HandleSubtype]string{
	fidlgen.HandleSubtypeBti:          "BTI",
	fidlgen.HandleSubtypeChannel:      "CHANNEL",
	fidlgen.HandleSubtypeClock:        "CLOCK",
	fidlgen.HandleSubtypeDebugLog:     "DEBUGLOG",
	fidlgen.HandleSubtypeEvent:        "EVENT",
	fidlgen.HandleSubtypeEventpair:    "EVENTPAIR",
	fidlgen.HandleSubtypeException:    "EXCEPTION",
	fidlgen.HandleSubtypeFifo:         "FIFO",
	fidlgen.HandleSubtypeGuest:        "GUEST",
	fidlgen.HandleSubtypeInterrupt:    "INTERRUPT",
	fidlgen.HandleSubtypeIommu:        "IOMMU",
	fidlgen.HandleSubtypeJob:          "JOB",
	fidlgen.HandleSubtypeMsi:          "MSI",
	fidlgen.HandleSubtypeNone:         "NONE",
	fidlgen.HandleSubtypePager:        "PAGER",
	fidlgen.HandleSubtypePmt:          "PMT",
	fidlgen.HandleSubtypePort:         "PORT",
	fidlgen.HandleSubtypeProcess:      "PROCESS",
	fidlgen.HandleSubtypeProfile:      "PROFILE",
	fidlgen.HandleSubtypeResource:     "RESOURCE",
	fidlgen.HandleSubtypeSocket:       "SOCKET",
	fidlgen.HandleSubtypeStream:       "STREAM",
	fidlgen.HandleSubtypeSuspendToken: "SUSPEND_TOKEN",
	fidlgen.HandleSubtypeThread:       "THREAD",
	fidlgen.HandleSubtypeTime:         "TIMER",
	fidlgen.HandleSubtypeVcpu:         "VCPU",
	fidlgen.HandleSubtypeVmar:         "VMAR",
	fidlgen.HandleSubtypeVmo:          "VMO",
}

type compiler struct {
	decls        fidlgen.DeclInfoMap
	experiments  fidlgen.Experiments
	library      fidlgen.LibraryIdentifier
	externCrates map[string]struct{}
	// Identifies which types are used as payload types, message body types, or
	// both.
	methodTypeUses fidlgen.MethodTypeUsageMap
	// Collection of structs used as a method payload, wire result, or both, as
	// described in the docs for fidlgen.MethodTypeUsage. This holds the
	// compiled fidlgen.Struct because compileResultFromUnion needs to access
	// the members from the compiled struct, and if the type is anonymous it
	// will not appear Root.Structs.
	methodTypeStructs      map[fidlgen.EncodedCompoundIdentifier]Struct
	structs                map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct
	results                map[fidlgen.EncodedCompoundIdentifier]Result
	handleMetadataWrappers map[string]HandleMetadataWrapper
}

// inExternalLibrary returns true if the library that the given
// CompoundIdentifier is in is different from the one the compiler is generating
// code for.
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

// TODO(fxbug.dev/66767): Escaping reserved words should happen *after*
// converting to CamelCase.
func compileCamelIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ToUpperCamelCase(changeIfReserved(val))
}

func compileLibraryName(library fidlgen.LibraryIdentifier) string {
	parts := []string{"fidl"}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(fidlgen.Identifier(strings.Join(parts, "_")))
}

// compileSnakeIdentifier converts the identifier to snake_case and escapes it
// (by adding an underscore suffix) if it collides with a reserved word.
// TODO(fxbug.dev/66767): Escaping reserved words should happen *after*
// converting to snake_case.
func compileSnakeIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ToSnakeCase(changeIfReserved(val))
}

// TODO(fxbug.dev/66767): Escaping reserved words should happen *after*
// converting to SCREAMING_SNAKE_CASE.
func compileScreamingSnakeIdentifier(val fidlgen.Identifier) string {
	return fidlgen.ConstNameToAllCapsSnake(changeIfReserved(val))
}

// compileCompoundIdentifier produces a string Rust identifier which can be used
// from the generated code to refer to the specified FIDL declaration or member.
//
// The case used in the Declaration and Member names will be unchanged.
//
// If the CompoundIdentifier is from the current library, the name will be
// unqualified, meaning that it is suitable for use in Rust type declarations.
// If it is from a different library, it will be fully qualified, and the source
// library will be added as a required extern-crate.
func (c *compiler) compileCompoundIdentifier(val fidlgen.CompoundIdentifier) string {
	strs := []string{}
	if c.inExternalLibrary(val) {
		externName := compileLibraryName(val.Library)
		c.externCrates[externName] = struct{}{}
		strs = append(strs, externName)
	}
	str := changeIfReserved(val.Name)
	strs = append(strs, str)
	if val.Member != "" {
		strs = append(strs, string(val.Member))
	}
	return strings.Join(strs, "::")
}

// compileCamelCompoundIdentifier produces a string Rust identifier which can be
// used from the generated code to refer to the specified FIDL declaration or
// member.
//
// The resulting string will have the Declaration changed to CamelCase, but
// Member (if any) will be unaffected by case conversion.
//
// If the CompoundIdentifier is from the current library, the name will be
// unqualified. If the CompoundIdentifier refers to a Declaration, that means
// that the name is just the declaration name converted to CamelCase, so it is
// suitable for use in Rust type declarations. If the CompoundIdentifier is for
// a member within a declaration, the member name will be qualified by the
// declaration name, so it is not suitable for declaring e.g. a method name.
//
// If it is from a different library, it will be fully qualified, and the source
// library will be added as a required extern-crate.
func (c *compiler) compileCamelCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := eci.Parse()
	val.Name = fidlgen.Identifier(compileCamelIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileSnakeCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := eci.Parse()
	val.Name = fidlgen.Identifier(compileSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func (c *compiler) compileScreamingSnakeCompoundIdentifier(eci fidlgen.EncodedCompoundIdentifier) string {
	val := eci.Parse()
	val.Name = fidlgen.Identifier(compileScreamingSnakeIdentifier(val.Name))
	return c.compileCompoundIdentifier(val)
}

func compileLiteral(val fidlgen.Literal, typ fidlgen.Type) string {
	switch val.Kind {
	case fidlgen.StringLiteral:
		var b strings.Builder
		b.WriteRune('"')
		for _, r := range val.Value {
			switch r {
			case '\\':
				b.WriteString(`\\`)
			case '"':
				b.WriteString(`\"`)
			case '\n':
				b.WriteString(`\n`)
			case '\r':
				b.WriteString(`\r`)
			case '\t':
				b.WriteString(`\t`)
			default:
				if unicode.IsPrint(r) {
					b.WriteRune(r)
				} else {
					b.WriteString(fmt.Sprintf(`\u{%x}`, r))
				}
			}
		}
		b.WriteRune('"')
		return b.String()
	case fidlgen.NumericLiteral:
		if typ.Kind == fidlgen.PrimitiveType &&
			(typ.PrimitiveSubtype == fidlgen.Float32 || typ.PrimitiveSubtype == fidlgen.Float64) {
			if !strings.ContainsRune(val.Value, '.') {
				return fmt.Sprintf("%s.0", val.Value)
			}
			return val.Value
		}
		return val.Value
	case fidlgen.BoolLiteral:
		return val.Value
	case fidlgen.DefaultLiteral:
		return "::Default::default()"
	default:
		panic(fmt.Sprintf("unknown literal kind: %v", val.Kind))
	}
}

func (c *compiler) identifierConstantDeclType(eci EncodedCompoundIdentifier) fidlgen.DeclType {
	memberless := eci.Parse()
	memberless.Member = ""
	declInfo, ok := c.decls[memberless.Encode()]
	if !ok {
		panic(fmt.Sprintf("identifier not in decl map: %s", memberless.Encode()))
	}
	return declInfo.Type
}

func (c *compiler) compileConstant(val fidlgen.Constant, typ fidlgen.Type) string {
	switch val.Kind {
	case fidlgen.IdentifierConstant:
		declType := c.identifierConstantDeclType(val.Identifier)
		parts := val.Identifier.Parse()
		switch declType {
		case fidlgen.ConstDeclType:
			parts.Name = fidlgen.Identifier(compileScreamingSnakeIdentifier(parts.Name))
			return c.compileCompoundIdentifier(parts)
		case fidlgen.BitsDeclType:
			parts.Name = fidlgen.Identifier(compileCamelIdentifier(parts.Name))
			parts.Member = fidlgen.Identifier(compileScreamingSnakeIdentifier(parts.Member))
			// TODO(fxbug.dev/93195): For now we assume the primitive type
			// matches the bits underlying type. If it doesn't the generated
			// Rust code will not compile.
			if typ.Kind == fidlgen.PrimitiveType {
				return fmt.Sprintf("%s.bits()", c.compileCompoundIdentifier(parts))
			}
			return c.compileCompoundIdentifier(parts)
		case fidlgen.EnumDeclType:
			parts.Name = fidlgen.Identifier(compileCamelIdentifier(parts.Name))
			parts.Member = fidlgen.Identifier(compileCamelIdentifier(parts.Member))
			// TODO(fxbug.dev/93195): For now we assume the primitive type
			// matches the enum underlying type. If it doesn't the generated
			// Rust code will not compile.
			if typ.Kind == fidlgen.PrimitiveType {
				return fmt.Sprintf("%s.into_primitive()", c.compileCompoundIdentifier(parts))
			}
			return c.compileCompoundIdentifier(parts)
		default:
			panic(fmt.Sprintf("unexpected decl type %s", declType))
		}
	case fidlgen.LiteralConstant:
		return compileLiteral(*val.Literal, typ)
	case fidlgen.BinaryOperator:
		if typ.Kind == fidlgen.PrimitiveType {
			return val.Value
		}
		decl := c.compileType(typ)
		// from_bits isn't a const function, so from_bits_truncate must be used.
		return fmt.Sprintf("%s::from_bits_truncate(%s)", decl, val.Value)
	default:
		panic(fmt.Sprintf("unknown constant kind: %s", val.Kind))
	}
}

func (c *compiler) compileConst(val fidlgen.Const) Const {
	name := c.compileScreamingSnakeCompoundIdentifier(val.Name)
	var r Const
	if val.Type.Kind == fidlgen.StringType {
		r = Const{
			Const: val,
			Type:  "&str",
			Name:  name,
			Value: c.compileConstant(val.Value, val.Type),
		}
	} else {
		r = Const{
			Const: val,
			Type:  c.compileType(val.Type),
			Name:  name,
			Value: c.compileConstant(val.Value, val.Type),
		}
	}
	return r
}

func compilePrimitiveSubtype(val fidlgen.PrimitiveSubtype) string {
	if t, ok := primitiveTypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

func compileHandleSubtype(val fidlgen.HandleSubtype) string {
	if t, ok := handleSubtypes[val]; ok {
		return t
	}
	panic(fmt.Sprintf("unknown handle type: %v", val))
}

type FieldHandleInformation struct {
	fullObjectType    string
	fullRights        string
	shortObjectType   string
	shortRights       string
	hasHandleMetadata bool
}

func (c *compiler) fieldHandleInformation(val *fidlgen.Type) FieldHandleInformation {
	if val.ElementType != nil {
		return c.fieldHandleInformation(val.ElementType)
	}
	if val.Kind == fidlgen.RequestType {
		return FieldHandleInformation{
			fullObjectType:    "fidl::ObjectType::CHANNEL",
			fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
			shortObjectType:   "CHANNEL",
			shortRights:       "CHANNEL_DEFAULT",
			hasHandleMetadata: true,
		}
	}
	if val.Kind == fidlgen.IdentifierType {
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		if declInfo.Type == fidlgen.ProtocolDeclType {
			return FieldHandleInformation{
				fullObjectType:    "fidl::ObjectType::CHANNEL",
				fullRights:        "fidl::Rights::CHANNEL_DEFAULT",
				shortObjectType:   "CHANNEL",
				shortRights:       "CHANNEL_DEFAULT",
				hasHandleMetadata: true,
			}
		}
	}
	if val.Kind != fidlgen.HandleType {
		return FieldHandleInformation{
			fullObjectType:    "fidl::ObjectType::NONE",
			fullRights:        "fidl::Rights::NONE",
			shortObjectType:   "NONE",
			shortRights:       "NONE",
			hasHandleMetadata: false,
		}
	}
	subtype, ok := handleSubtypeConsts[val.HandleSubtype]
	if !ok {
		panic(fmt.Sprintf("unknown handle type for const: %v", val))
	}
	return FieldHandleInformation{
		fullObjectType:    fmt.Sprintf("fidl::ObjectType::%s", subtype),
		fullRights:        fmt.Sprintf("fidl::Rights::from_bits_const(%d).unwrap()", val.HandleRights),
		shortObjectType:   subtype,
		shortRights:       fmt.Sprintf("%d", val.HandleRights),
		hasHandleMetadata: true,
	}
}

// compileType gets a string to use in generated code for the rust type used for
// the given FIDL type.
func (c *compiler) compileType(val fidlgen.Type) string {
	var t string
	switch val.Kind {
	case fidlgen.ArrayType:
		t = fmt.Sprintf("[%s; %v]", c.compileType(*val.ElementType), *val.ElementCount)
	case fidlgen.VectorType:
		t = fmt.Sprintf("Vec<%s>", c.compileType(*val.ElementType))
	case fidlgen.StringType:
		t = "String"
	case fidlgen.HandleType:
		t = fmt.Sprintf("fidl::%s", compileHandleSubtype(val.HandleSubtype))
	case fidlgen.RequestType:
		t = fmt.Sprintf("fidl::endpoints::ServerEnd<%sMarker>", c.compileCamelCompoundIdentifier(val.RequestSubtype))
	case fidlgen.PrimitiveType:
		t = compilePrimitiveSubtype(val.PrimitiveSubtype)
	case fidlgen.IdentifierType:
		t = c.compileCamelCompoundIdentifier(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.UnionDeclType:
			if val.Nullable {
				t = fmt.Sprintf("Box<%s>", t)
			}
		case fidlgen.ProtocolDeclType:
			t = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}

	if val.Nullable {
		return fmt.Sprintf("Option<%s>", t)
	}
	return t
}

// compileBorrowedType gets a string to use in generated code for the rust type
// used when the given FIDL type is used in a method parameter where encoding is
// needed. Note that borrowing is only actually used for structs and collections
// where we want to avoid copying, primitive types are passed by value.
// Generated references are mutable in order to allow fuchsia handles to be
// moved out when encoding.
func (c *compiler) compileBorrowedType(val fidlgen.Type) string {
	var t string
	switch val.Kind {
	case fidlgen.PrimitiveType, fidlgen.HandleType, fidlgen.RequestType:
		return c.compileType(val)
	case fidlgen.ArrayType:
		t = fmt.Sprintf("&mut [%s; %v]", c.compileBorrowedType(*val.ElementType), *val.ElementCount)
	case fidlgen.VectorType:
		t = c.compileBorrowedType(*val.ElementType)
		// We use slices for primitive numeric types so that encoding becomes a
		// memcpy. Rust does not guarantee the bit patterns for bool values, so
		// we omit them from the optimization.
		if val.ElementType.Kind == fidlgen.PrimitiveType && val.ElementType.PrimitiveSubtype != fidlgen.Bool {
			t = fmt.Sprintf("&[%s]", t)
		} else {
			t = fmt.Sprintf("&mut dyn ExactSizeIterator<Item = %s>", t)
		}
	case fidlgen.StringType:
		t = "&str"
	case fidlgen.IdentifierType:
		t = c.compileCamelCompoundIdentifier(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.UnionDeclType:
			t = fmt.Sprintf("&mut %s", t)
		case fidlgen.ProtocolDeclType:
			t = fmt.Sprintf("fidl::endpoints::ClientEnd<%sMarker>", t)
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}

	if val.Nullable {
		return fmt.Sprintf("Option<%s>", t)
	}
	return t
}

func (c *compiler) compileBits(val fidlgen.Bits) Bits {
	e := Bits{
		Bits:    val,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Type:    c.compileType(val.Type),
		Members: []BitsMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, BitsMember{
			BitsMember: v,
			Name:       compileScreamingSnakeIdentifier(v.Name),
			Value:      c.compileConstant(v.Value, val.Type),
		})
	}
	return e
}

func (c *compiler) compileEnum(val fidlgen.Enum) Enum {
	e := Enum{
		Enum:    val,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Type:    compilePrimitiveSubtype(val.Type),
		Members: []EnumMember{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, EnumMember{
			EnumMember: v,
			Name:       compileCamelIdentifier(v.Name),
			Value:      v.Value.Value,
		})
	}
	e.MinMember = findMinEnumMember(val.Type, e.Members).Name
	return e
}

func findMinEnumMember(typ fidlgen.PrimitiveSubtype, members []EnumMember) EnumMember {
	var res EnumMember
	if typ.IsSigned() {
		min := int64(math.MaxInt64)
		for _, m := range members {
			v, err := strconv.ParseInt(m.Value, 10, 64)
			if err != nil {
				panic(fmt.Sprintf("invalid enum member value: %s", err))
			}
			if v < min {
				min = v
				res = m
			}
		}
	} else {
		min := uint64(math.MaxUint64)
		for _, m := range members {
			v, err := strconv.ParseUint(m.Value, 10, 64)
			if err != nil {
				panic(fmt.Sprintf("invalid enum member value: %s", err))
			}
			if v < min {
				min = v
				res = m
			}
		}
	}
	return res
}

func (c *compiler) compileHandleMetadataWrapper(val *fidlgen.Type) (string, bool) {
	hi := c.fieldHandleInformation(val)
	name := fmt.Sprintf("HandleWrapperObjectType%sRights%s", hi.shortObjectType, hi.shortRights)
	wrapper := HandleMetadataWrapper{
		Name:    name,
		Subtype: hi.fullObjectType,
		Rights:  hi.fullRights,
	}
	if hi.hasHandleMetadata {
		if _, ok := c.handleMetadataWrappers[name]; !ok {
			c.handleMetadataWrappers[name] = wrapper
		}
	}
	return name, hi.hasHandleMetadata
}

// compileParameterArray extracts the fields of the given method-request or
// method-response payload struct as method parameters, and prepares them for
// code generation by providing rust types and names to use.
func (c *compiler) compileParameterArray(payload fidlgen.Struct) []Parameter {
	var parameters []Parameter
	for _, v := range payload.Members {
		wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&v.Type)
		parameters = append(parameters, Parameter{
			OGType:            v.Type,
			Type:              c.compileType(v.Type),
			BorrowedType:      c.compileBorrowedType(v.Type),
			Name:              compileSnakeIdentifier(v.Name),
			HandleWrapperName: wrapperName,
			HasHandleMetadata: hasHandleMetadata,
		})
	}
	return parameters
}

// compileParameterSingleton converts a union or table payload into a form that
// is ready for code generation, providing a Rust-friendly type and name (always
// "payload"). Unlike compileParameterArray, this does not result in the payload
// layout being "flattened" into its constituent members.
func (c *compiler) compileParameterSingleton(val *fidlgen.Type) []Parameter {
	wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(val)
	return []Parameter{{
		OGType:            *val,
		Type:              c.compileType(*val),
		BorrowedType:      c.compileBorrowedType(*val),
		Name:              "payload",
		HandleWrapperName: wrapperName,
		HasHandleMetadata: hasHandleMetadata,
	}}
}

// TODO(fxbug.dev/76655): Remove this.
const maximumAllowedParameters = 12

func (c *compiler) compileProtocol(val fidlgen.Protocol) Protocol {
	r := Protocol{
		Protocol:     val,
		ECI:          val.Name,
		Name:         c.compileCamelCompoundIdentifier(val.Name),
		Methods:      []Method{},
		ProtocolName: strings.Trim(val.GetProtocolName(), "\""),
	}

	getParametersFromType := func(t *fidlgen.Type) []Parameter {
		if _, ok := c.methodTypeUses[t.Identifier]; ok {
			if val, ok := c.methodTypeStructs[t.Identifier]; ok {
				return c.compileParameterArray(val.Struct)
			}
			return c.compileParameterSingleton(t)
		}
		panic(fmt.Sprintf("unknown request/response layout: %v", t.Identifier))
	}

	for _, v := range val.Methods {
		var compiledRequestParameterList []Parameter
		if v.RequestPayload != nil {
			compiledRequestParameterList = getParametersFromType(v.RequestPayload)
			if len(compiledRequestParameterList) > maximumAllowedParameters {
				panic(fmt.Sprintf(
					`Method %s.%s has %d parameters, but the FIDL Rust bindings `+
						`only support up to %d. See https://fxbug.dev/76655 for details.`,
					val.Name, v.Name, len(compiledRequestParameterList), maximumAllowedParameters))
			}
		}

		name := compileSnakeIdentifier(v.Name)
		camelName := compileCamelIdentifier(v.Name)
		var compiledResponseParameterList []Parameter
		var foundResult *Result

		findResultType := func() *Result {
			if result, ok := c.results[v.ResultType.Identifier]; ok {
				return &result
			}
			panic(fmt.Sprintf("unknown result type: %v", v.ResultType.Identifier))
		}

		if v.HasError {
			compiledResponseParameterList = getParametersFromType(v.ResponsePayload)
			foundResult = findResultType()
		} else if v.HasTransportError() {
			compiledResponseParameterList = getParametersFromType(v.ValueType)
			foundResult = findResultType()
		} else if v.ResponsePayload != nil {
			compiledResponseParameterList = getParametersFromType(v.ResponsePayload)
		}

		// TODO(fxbug.dev/106641): This feature is currently restricted to
		// allowlisted libraries. It is a "dirty" implementation of the final
		// feature, with some checks (like determining whether or not overflowing
		// checks are needed for both encode and decode, rather than just the
		// latter) being omitted.
		overflowing := Overflowing{}
		if c.experiments.Contains(fidlgen.ExperimentAllowOverflowing) {
			attr, found := v.Attributes.LookupAttribute("experimental_overflowing")
			if found {
				reqArg, hasReq := attr.LookupArg("request")
				if hasReq && reqArg.ValueString() != "false" {
					overflowing.OnRequestEncode = true
					overflowing.OnRequestDecode = true
				}
				resArg, hasRes := attr.LookupArg("response")
				if hasRes && resArg.ValueString() != "false" {
					overflowing.OnResponseEncode = true
					overflowing.OnResponseDecode = true
				}
			}
		}

		r.Methods = append(r.Methods, Method{
			Method:      v,
			Name:        name,
			CamelName:   camelName,
			Request:     compiledRequestParameterList,
			Response:    compiledResponseParameterList,
			Result:      foundResult,
			Overflowing: overflowing,
		})
	}

	return r
}

func (c *compiler) compileService(val fidlgen.Service) Service {
	r := Service{
		Service:     val,
		Name:        c.compileCamelCompoundIdentifier(val.Name),
		Members:     []ServiceMember{},
		ServiceName: val.GetServiceName(),
	}

	for _, v := range val.Members {
		m := ServiceMember{
			ServiceMember:     v,
			Name:              string(v.Name),
			CamelName:         compileCamelIdentifier(v.Name),
			SnakeName:         compileSnakeIdentifier(v.Name),
			ProtocolType:      c.compileCamelCompoundIdentifier(v.Type.Identifier),
			ProtocolTransport: v.Type.ProtocolTransport,
		}
		r.Members = append(r.Members, m)
	}

	return r
}

func (c *compiler) compileStructMember(val fidlgen.StructMember) StructMember {
	hi := c.fieldHandleInformation(&val.Type)
	return StructMember{
		StructMember:      val,
		Type:              c.compileType(val.Type),
		OGType:            val.Type,
		Name:              compileSnakeIdentifier(val.Name),
		OffsetV1:          val.FieldShapeV1.Offset,
		OffsetV2:          val.FieldShapeV2.Offset,
		HasDefault:        false,
		DefaultValue:      "", // TODO(cramertj) support defaults
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func (c *compiler) computeUseFidlStructCopyForStruct(st fidlgen.Struct) bool {
	if len(st.Members) == 0 {
		// In Rust, structs containing empty structs do not match the C++ struct layout
		// since empty structs have size 0 in Rust -- even in repr(C).
		return false
	}
	for _, member := range st.Members {
		if !c.computeUseFidlStructCopy(&member.Type) {
			return false
		}
	}
	return true
}

func (c *compiler) computeUseFidlStructCopy(typ *fidlgen.Type) bool {
	if typ.Nullable {
		return false
	}
	switch typ.Kind {
	case fidlgen.ArrayType:
		return c.computeUseFidlStructCopy(typ.ElementType)
	case fidlgen.VectorType, fidlgen.StringType, fidlgen.HandleType, fidlgen.RequestType:
		return false
	case fidlgen.PrimitiveType:
		switch typ.PrimitiveSubtype {
		case fidlgen.Bool, fidlgen.Float32, fidlgen.Float64:
			return false
		}
		return true
	case fidlgen.IdentifierType:
		if c.inExternalLibrary(typ.Identifier.Parse()) {
			return false
		}
		declType := c.decls[typ.Identifier].Type
		switch declType {
		case fidlgen.BitsDeclType, fidlgen.EnumDeclType, fidlgen.TableDeclType, fidlgen.UnionDeclType, fidlgen.ProtocolDeclType:
			return false
		case fidlgen.StructDeclType:
			st, ok := c.structs[typ.Identifier]
			if !ok {
				panic(fmt.Sprintf("struct not found: %v", typ.Identifier))
			}
			return c.computeUseFidlStructCopyForStruct(st)
		default:
			panic(fmt.Sprintf("unknown declaration type: %v", declType))
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", typ.Kind))
	}
}

func (c *compiler) resolveStruct(identifier fidlgen.EncodedCompoundIdentifier) *fidlgen.Struct {
	if c.inExternalLibrary(identifier.Parse()) {
		// This behavior is matched by computeUseFullStructCopy.
		return nil
	}
	declType := c.decls[identifier].Type
	if declType == fidlgen.StructDeclType {
		st, ok := c.structs[identifier]
		if !ok {
			panic(fmt.Sprintf("struct not found: %v", identifier))
		}
		return &st
	}
	return nil
}

type rustPaddingMarker struct {
	Type   string
	Offset int
	// Mask is a string so it can be in hex.
	Mask string
}

func toRustPaddingMarker(in fidlgen.PaddingMarker) rustPaddingMarker {
	switch len(in.Mask) {
	case 2:
		return rustPaddingMarker{
			Type:   "u16",
			Offset: in.Offset,
			Mask:   fmt.Sprintf("0x%04xu16", binary.LittleEndian.Uint16(in.Mask)),
		}
	case 4:
		return rustPaddingMarker{
			Type:   "u32",
			Offset: in.Offset,
			Mask:   fmt.Sprintf("0x%08xu32", binary.LittleEndian.Uint32(in.Mask)),
		}
	case 8:
		return rustPaddingMarker{
			Type:   "u64",
			Offset: in.Offset,
			Mask:   fmt.Sprintf("0x%016xu64", binary.LittleEndian.Uint64(in.Mask)),
		}
	default:
		panic("unexpected mask size")
	}
}

func toRustPaddingMarkers(in []fidlgen.PaddingMarker) []rustPaddingMarker {
	var out []rustPaddingMarker
	for _, m := range in {
		out = append(out, toRustPaddingMarker(m))
	}
	return out
}

func (c *compiler) compileStruct(val fidlgen.Struct) Struct {
	name := c.compileCamelCompoundIdentifier(val.Name)
	r := Struct{
		Struct:                    val,
		ECI:                       val.Name,
		Name:                      name,
		Members:                   []StructMember{},
		SizeV1:                    val.TypeShapeV1.InlineSize,
		SizeV2:                    val.TypeShapeV2.InlineSize,
		AlignmentV1:               val.TypeShapeV1.Alignment,
		AlignmentV2:               val.TypeShapeV2.Alignment,
		PaddingMarkersV1:          toRustPaddingMarkers(val.BuildPaddingMarkers(fidlgen.WireFormatVersionV1)),
		PaddingMarkersV2:          toRustPaddingMarkers(val.BuildPaddingMarkers(fidlgen.WireFormatVersionV2)),
		FlattenedPaddingMarkersV1: toRustPaddingMarkers(val.BuildFlattenedPaddingMarkers(fidlgen.WireFormatVersionV1, c.resolveStruct)),
		FlattenedPaddingMarkersV2: toRustPaddingMarkers(val.BuildFlattenedPaddingMarkers(fidlgen.WireFormatVersionV2, c.resolveStruct)),
	}

	for _, v := range val.Members {
		member := c.compileStructMember(v)
		r.Members = append(r.Members, member)
		r.HasPadding = r.HasPadding || (v.FieldShapeV1.Padding != 0)
	}

	r.UseFidlStructCopy = c.computeUseFidlStructCopyForStruct(val)

	return r
}

func (c *compiler) compileUnionMember(val fidlgen.UnionMember) UnionMember {
	hi := c.fieldHandleInformation(val.Type)
	return UnionMember{
		UnionMember:       val,
		Type:              c.compileType(*val.Type),
		OGType:            *val.Type,
		Name:              compileCamelIdentifier(val.Name),
		Ordinal:           val.Ordinal,
		HasHandleMetadata: hi.hasHandleMetadata,
		HandleSubtype:     hi.fullObjectType,
		HandleRights:      hi.fullRights,
	}
}

func (c *compiler) compileUnion(val fidlgen.Union) Union {
	r := Union{
		Union:   val,
		ECI:     val.Name,
		Name:    c.compileCamelCompoundIdentifier(val.Name),
		Members: []UnionMember{},
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	return r
}

func (c *compiler) compileResultFromUnion(m fidlgen.Method, root Root) Result {
	// Results may be compiled more than once if they are composed. In that
	// case, just re use the existing value.
	if r, ok := c.results[m.ResultType.Identifier]; ok {
		return r
	}

	r := Result{
		ECI:               m.ResultType.Identifier,
		Name:              c.compileCamelCompoundIdentifier(m.ResultType.Identifier),
		HasTransportError: m.HasTransportError(),
	}

	if m.HasError {
		r.ErrOGType = m.ErrorType
		errType := c.compileType(*m.ErrorType)
		r.ErrType = &errType
	}
	declInfo, ok := c.decls[m.ValueType.Identifier]
	if !ok {
		panic(fmt.Sprintf("declaration not found: %v", m.ValueType.Identifier))
	}

	switch declInfo.Type {
	case fidlgen.StructDeclType:
		for _, m := range c.methodTypeStructs[m.ValueType.Identifier].Members {
			wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(&m.OGType)
			r.Ok = append(r.Ok, ResultOkEntry{
				OGType:            m.OGType,
				Type:              m.Type,
				HasHandleMetadata: hasHandleMetadata,
				HandleWrapperName: wrapperName,
			})
		}
	case fidlgen.TableDeclType, fidlgen.UnionDeclType:
		wrapperName, hasHandleMetadata := c.compileHandleMetadataWrapper(m.ValueType)
		r.Ok = append(r.Ok, ResultOkEntry{
			OGType:            *m.ValueType,
			Type:              c.compileType(*m.ValueType),
			HasHandleMetadata: hasHandleMetadata,
			HandleWrapperName: wrapperName,
		})
	default:
		panic("payload must be struct, table, or union")
	}

	c.results[r.ECI] = r

	return r
}

func (c *compiler) compileTable(table fidlgen.Table) Table {
	var members []TableMember
	for _, member := range table.SortedMembersNoReserved() {
		hi := c.fieldHandleInformation(member.Type)
		members = append(members, TableMember{
			TableMember:       member,
			OGType:            *member.Type,
			Type:              c.compileType(*member.Type),
			Name:              compileSnakeIdentifier(member.Name),
			Ordinal:           member.Ordinal,
			HasHandleMetadata: hi.hasHandleMetadata,
			HandleSubtype:     hi.fullObjectType,
			HandleRights:      hi.fullRights,
		})
	}
	return Table{
		Table:   table,
		ECI:     table.Name,
		Name:    c.compileCamelCompoundIdentifier(table.Name),
		Members: members,
	}
}

type derives uint16

const (
	derivesDebug derives = 1 << iota
	derivesCopy
	derivesClone
	derivesEq
	derivesPartialEq
	derivesOrd
	derivesPartialOrd
	derivesHash
	derivesAsBytes
	derivesFromBytes
	derivesAll derives = (1 << iota) - 1

	derivesMinimal            derives = derivesDebug | derivesPartialEq
	derivesMinimalNonResource derives = derivesMinimal | derivesClone
	derivesAllButZerocopy     derives = derivesAll & ^derivesAsBytes & ^derivesFromBytes
)

// note: keep this list in the same order as the derives definitions
var derivesNames = []string{
	// [START derived_traits]
	"Debug",
	"Copy",
	"Clone",
	"Eq",
	"PartialEq",
	"Ord",
	"PartialOrd",
	"Hash",
	"zerocopy::AsBytes",
	"zerocopy::FromBytes",
	// [END derived_traits]
}

func (v derives) and(others derives) derives {
	return v & others
}

func (v derives) remove(others ...derives) derives {
	result := v
	for _, other := range others {
		result &= ^other
	}
	return result
}

func (v derives) andUnknown() derives {
	return v.and(derivesMinimal)
}

func (v derives) andUnknownNonResource() derives {
	return v.and(derivesMinimalNonResource)
}

func (v derives) contains(other derives) bool {
	return (v & other) != 0
}

func (v derives) String() string {
	var parts []string
	for i, bit := 0, derives(1); bit&derivesAll != 0; i, bit = i+1, bit<<1 {
		if v.contains(bit) {
			parts = append(parts, derivesNames[i])
		}
	}
	if len(parts) == 0 {
		return ""
	}
	return fmt.Sprintf("#[derive(%s)]", strings.Join(parts, ", "))
}

// The status of derive calculation for a particular type.
type deriveStatus struct {
	// recursing indicates whether or not we've passed through this type already
	// on a recursive descent. This is used to prevent unbounded recursion on
	// mutually-recursive types.
	recursing bool
	// complete indicates whether or not the derive for the given type has
	// already been successfully calculated and stored in the IR.
	complete bool
}

// The state of the current calculation of derives.
type derivesCompiler struct {
	*compiler
	topMostCall                bool
	didShortCircuitOnRecursion bool
	statuses                   map[EncodedCompoundIdentifier]deriveStatus
	root                       *Root
}

// [START fill_derives]
// Calculates what traits should be derived for each output type,
// filling in all `*derives` in the IR.
func (c *compiler) fillDerives(ir *Root) {
	// [END fill_derives]
	dc := &derivesCompiler{
		compiler:                   c,
		topMostCall:                true,
		didShortCircuitOnRecursion: false,
		statuses:                   make(map[EncodedCompoundIdentifier]deriveStatus),
		root:                       ir,
	}

	// Bits and enums always derive all traits
	for _, v := range ir.Protocols {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Structs {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.ExternalStructs {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Unions {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Results {
		dc.fillDerivesForECI(v.ECI)
	}
	for _, v := range ir.Tables {
		dc.fillDerivesForECI(v.ECI)
	}
}

func (dc *derivesCompiler) fillDerivesForECI(eci EncodedCompoundIdentifier) derives {
	declInfo, ok := dc.decls[eci]
	if !ok {
		panic(fmt.Sprintf("declaration not found: %v", eci))
	}

	// TODO(fxbug.dev/61760): Make external type information available here.
	// Currently, we conservatively assume external structs/tables/unions only
	// derive the minimal set of traits, plus Clone for value types (not having
	// Clone is especially annoying, so we put resourceness of external types
	// into the IR as a stopgap solution).
	if dc.inExternalLibrary(eci.Parse()) {
		switch declInfo.Type {
		case fidlgen.StructDeclType, fidlgen.TableDeclType, fidlgen.UnionDeclType:
			if declInfo.IsValueType() {
				return derivesMinimalNonResource
			}
			return derivesMinimal
		}
	}

	// Return early for declaration types that do not require recursion.
	switch declInfo.Type {
	case fidlgen.ConstDeclType:
		panic("const decl should never have derives")
	case fidlgen.BitsDeclType, fidlgen.EnumDeclType:
		// Enums and bits are always simple, non-float primitives which
		// implement all derivable traits except zerocopy.
		return derivesAllButZerocopy
	case fidlgen.ProtocolDeclType:
		// When a protocol is used as a type, it means a ClientEnd in Rust.
		return derivesAllButZerocopy.remove(derivesCopy, derivesClone)
	}

	topMostCall := dc.topMostCall
	if dc.topMostCall {
		dc.topMostCall = false
	}
	deriveStatus := dc.statuses[eci]
	if deriveStatus.recursing {
		// If we've already seen the current type while recursing,
		// the algorithm has already explored all of the other fields contained
		// within the cycle, so we can return true for all derives, and the
		// correct results will be bubbled up.
		dc.didShortCircuitOnRecursion = true
		return derivesAll
	}
	deriveStatus.recursing = true
	dc.statuses[eci] = deriveStatus

	var derivesOut derives
typeSwitch:
	switch declInfo.Type {
	case fidlgen.StructDeclType:
		st := dc.root.findStruct(eci, false)
		if st == nil {
			panic(fmt.Sprintf("struct not found: %v", eci))
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = st.Derives
			break typeSwitch
		}
		derivesOut = derivesAll
		for _, member := range st.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		if st.HasPadding || len(st.Members) == 0 {
			derivesOut = derivesOut.remove(derivesAsBytes, derivesFromBytes)
		}
		st.Derives = derivesOut
	case fidlgen.TableDeclType:
		table := dc.root.findTable(eci)
		if table == nil {
			panic(fmt.Sprintf("table not found: %v", eci))
		}
		// Check if the derives have already been calculated
		if deriveStatus.complete {
			derivesOut = table.Derives
			break typeSwitch
		}
		derivesOut = derivesAllButZerocopy
		for _, member := range table.Members {
			derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
		}
		if table.IsResourceType() {
			derivesOut = derivesOut.andUnknown()
		} else {
			derivesOut = derivesOut.andUnknownNonResource()
		}
		table.Derives = derivesOut
	case fidlgen.UnionDeclType:
		if result := dc.root.findResult(eci); result != nil {
			// It's a Result, not a union
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = result.Derives
				break typeSwitch
			}
			derivesOut = derivesAllButZerocopy
			for _, ok := range result.Ok {
				derivesOut = derivesOut.and(dc.fillDerivesForType(ok.OGType))
			}
			if result.ErrOGType != nil {
				derivesOut = derivesOut.and(dc.fillDerivesForType(*result.ErrOGType))
			}
			result.Derives = derivesOut
		} else if union := dc.root.findUnion(eci); union != nil {
			// Check if the derives have already been calculated
			if deriveStatus.complete {
				derivesOut = union.Derives
				break typeSwitch
			}
			derivesOut = derivesAllButZerocopy
			for _, member := range union.Members {
				derivesOut = derivesOut.and(dc.fillDerivesForType(member.OGType))
			}
			if union.IsFlexible() {
				if union.IsResourceType() {
					derivesOut = derivesOut.andUnknown()
				} else {
					derivesOut = derivesOut.andUnknownNonResource()
				}
			}
			union.Derives = derivesOut
		} else {
			panic(fmt.Sprintf("union not found: %v", eci))
		}
	default:
		panic(fmt.Sprintf("unknown declaration type: %v", declInfo.Type))
	}
	if topMostCall || !dc.didShortCircuitOnRecursion {
		// Our completed result is only valid if it's either at top-level
		// (ensuring we've fully visited all child types in the recursive
		// substructure at least once) or if we performed no recursion-based
		// short-circuiting, in which case results are correct and absolute.
		//
		// Note that non-topMostCalls are invalid if we did short-circuit
		// on recursion, because we might have short-circuited just beneath
		// a type without having explored all of its children at least once
		// beneath it.
		//
		// For example, imagine A -> B -> C -> A.
		// When we start on A, we go to B, then go to C, then go to A, at which
		// point we short-circuit. The intermediate result for C is invalid
		// for anything except the computation of A and B above, as it does
		// not take into account that C contains A and B, only that it contains
		// its top-level fields (other than A). In order to get a correct
		// idea of the shape of C, we have to start with C, following through
		// every branch until we find C again.
		deriveStatus.complete = true
	}
	if topMostCall {
		// Reset intermediate state
		dc.topMostCall = true
		dc.didShortCircuitOnRecursion = false
	}
	deriveStatus.recursing = false
	dc.statuses[eci] = deriveStatus
	return derivesOut
}

func (dc *derivesCompiler) fillDerivesForType(ogType fidlgen.Type) derives {
	switch ogType.Kind {
	case fidlgen.ArrayType:
		return dc.fillDerivesForType(*ogType.ElementType)
	case fidlgen.VectorType:
		return derivesAllButZerocopy.remove(derivesCopy).and(dc.fillDerivesForType(*ogType.ElementType))
	case fidlgen.StringType:
		return derivesAllButZerocopy.remove(derivesCopy)
	case fidlgen.HandleType, fidlgen.RequestType:
		return derivesAllButZerocopy.remove(derivesCopy, derivesClone)
	case fidlgen.PrimitiveType:
		switch ogType.PrimitiveSubtype {
		case fidlgen.Bool:
			return derivesAllButZerocopy
		case fidlgen.Int8, fidlgen.Int16, fidlgen.Int32, fidlgen.Int64:
			return derivesAll
		case fidlgen.Uint8, fidlgen.Uint16, fidlgen.Uint32, fidlgen.Uint64:
			return derivesAll
		case fidlgen.Float32, fidlgen.Float64:
			// Floats don't have a total ordering due to NAN and its multiple representations.
			return derivesAllButZerocopy.remove(derivesEq, derivesOrd, derivesHash)
		default:
			panic(fmt.Sprintf("unknown primitive type: %v", ogType.PrimitiveSubtype))
		}
	case fidlgen.IdentifierType:
		internalTypeDerives := dc.fillDerivesForECI(ogType.Identifier)
		if ogType.Nullable {
			// A nullable struct/union gets put in Option<Box<...>> and so
			// cannot derive Copy, AsBytes, or FromBytes. Bits, enums, and
			// tables are never nullable. The only other possibility for an
			// identifier type is a protocol, which cannot derive these either.
			return internalTypeDerives.remove(derivesCopy, derivesAsBytes, derivesFromBytes)
		}
		return internalTypeDerives
	default:
		panic(fmt.Sprintf("unknown type kind: %v", ogType.Kind))
	}
}

func Compile(r fidlgen.Root) Root {
	r = r.ForBindings("rust")
	root := Root{
		Experiments: r.Experiments,
	}
	thisLibParsed := r.Name.Parse()
	c := compiler{
		decls:                  r.DeclInfo(),
		experiments:            r.Experiments,
		library:                thisLibParsed,
		externCrates:           map[string]struct{}{},
		methodTypeUses:         r.MethodTypeUsageMap(),
		methodTypeStructs:      map[fidlgen.EncodedCompoundIdentifier]Struct{},
		structs:                map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{},
		results:                map[fidlgen.EncodedCompoundIdentifier]Result{},
		handleMetadataWrappers: map[string]HandleMetadataWrapper{},
	}

	for _, s := range r.Structs {
		c.structs[s.Name] = s
	}

	for _, s := range r.ExternalStructs {
		c.structs[s.Name] = s
	}

	for _, v := range r.Bits {
		root.Bits = append(root.Bits, c.compileBits(v))
	}

	for _, v := range r.Consts {
		root.Consts = append(root.Consts, c.compileConst(v))
	}

	for _, v := range r.Enums {
		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range r.Services {
		root.Services = append(root.Services, c.compileService(v))
	}

	for _, v := range r.Structs {
		compiled := c.compileStruct(v)
		if _, ok := c.methodTypeUses[v.Name]; ok {
			c.methodTypeStructs[v.Name] = compiled
			if v.IsAnonymous() {
				continue
			}
		}
		root.Structs = append(root.Structs, compiled)
	}

	for _, v := range r.ExternalStructs {
		compiled := c.compileStruct(v)
		if _, ok := c.methodTypeUses[v.Name]; ok {
			c.methodTypeStructs[v.Name] = compiled
			if v.IsAnonymous() {
				continue
			}
		}
		root.ExternalStructs = append(root.ExternalStructs, compiled)
	}

	for _, v := range r.Tables {
		root.Tables = append(root.Tables, c.compileTable(v))
	}

	for _, v := range r.Protocols {
		for _, m := range v.Methods {
			// A result is referenced multiple times when a method is composed.
			// We always need to compile the result from the union, because it
			// will be referenced when compiling the method and affects how the
			// method handles its arguments and return value, so we always run
			// compileResultFromUnion to ensure that c.results contains the
			// result union.
			//
			// However, we only want to actually generate the type aliases for
			// the result union once, and we especially don't want to generate
			// aliases when those already exist in another generated library, so
			// we don't add the result type to root.Results unless this is the
			// original non-composed method.
			if m.ResultType != nil {
				result := c.compileResultFromUnion(m, root)
				if !m.IsComposed {
					root.Results = append(root.Results, result)
				}
			}
		}
	}

	for _, v := range r.Unions {
		if result := root.findResult(v.Name); result == nil {
			root.Unions = append(root.Unions, c.compileUnion(v))
		}
	}

	for _, v := range r.Protocols {
		root.Protocols = append(root.Protocols, c.compileProtocol(v))
	}

	// Sort by wrapper name for deterministic output order.
	handleMetadataWrapperNames := make([]string, 0, len(c.handleMetadataWrappers))
	for name := range c.handleMetadataWrappers {
		handleMetadataWrapperNames = append(handleMetadataWrapperNames, name)
	}
	sort.Strings(handleMetadataWrapperNames)
	for _, name := range handleMetadataWrapperNames {
		root.HandleMetadataWrappers = append(root.HandleMetadataWrappers, c.handleMetadataWrappers[name])
	}

	c.fillDerives(&root)

	thisLibCompiled := compileLibraryName(thisLibParsed)

	// Sort the extern crates to make sure the generated file is
	// consistent across builds.
	var externCrates []string
	for k := range c.externCrates {
		externCrates = append(externCrates, k)
	}
	sort.Strings(externCrates)

	for _, k := range externCrates {
		if k != thisLibCompiled {
			root.ExternCrates = append(root.ExternCrates, k)
		}
	}

	return root
}
