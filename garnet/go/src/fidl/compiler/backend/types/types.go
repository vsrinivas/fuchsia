// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"sort"
	"strconv"
	"strings"
)

/*
This file contains types which describe FIDL protocols.

These types are intended to be directly deserialized from the FIDL protocol
JSON representation. The types are then passed directly to language-specific
generators which produce source code.

Note that these are different from a naive AST-based representation of
FIDL text. Before being transformed into JSON, FIDL sources are preprocessed
to generate metadata required by all of the backends, such as the size of
types. Importantly, this removes the need for language-specific backends to
implement field, name, or type resolution and analysis.
*/

// ReadJSONIr reads a JSON IR file.
func ReadJSONIr(filename string) (Root, error) {
	bytes, err := ioutil.ReadFile(filename)
	if err != nil {
		return Root{}, fmt.Errorf("Error reading from %s: %w", filename, err)
	}
	return ReadJSONIrContent(bytes)
}

// ReadJSONIrContent reads JSON IR content.
func ReadJSONIrContent(bytes []byte) (Root, error) {
	var root Root

	if err := json.Unmarshal(bytes, &root); err != nil {
		return root, fmt.Errorf("Error parsing JSON IR: %v", err)
	}

	// TODO(fxbug.dev/50195): This is for backward compatibility with fidlgen_dart in
	// Topaz, and should be removed after fidlgen_dart code has been updated.
	root.Interfaces = root.Protocols

	return root, nil
}

type Identifier string

type LibraryIdentifier []Identifier

type CompoundIdentifier struct {
	Library LibraryIdentifier
	Name    Identifier
	Member  Identifier
}

type EncodedLibraryIdentifier string

type EncodedCompoundIdentifier string

func (eli EncodedLibraryIdentifier) Parts() LibraryIdentifier {
	return ParseLibraryName(eli)
}

func (eli EncodedLibraryIdentifier) PartsReversed() []string {
	parts := eli.Parts()
	partsReversed := make([]string, len(parts))
	for i, part := range parts {
		partsReversed[len(parts)-i-1] = string(part)
	}

	return partsReversed
}

func (eci EncodedCompoundIdentifier) Parts() CompoundIdentifier {
	return ParseCompoundIdentifier(eci)
}

func (eci EncodedCompoundIdentifier) LibraryName() EncodedLibraryIdentifier {
	parts := strings.SplitN(string(eci), "/", 2)
	raw_library := ""
	if len(parts) == 2 {
		raw_library = parts[0]
	}
	return EncodedLibraryIdentifier(raw_library)
}

func ParseLibraryName(eli EncodedLibraryIdentifier) LibraryIdentifier {
	raw_parts := strings.Split(string(eli), ".")
	parts := make([]Identifier, len(raw_parts))
	for i, raw_part := range raw_parts {
		parts[i] = Identifier(raw_part)
	}
	return LibraryIdentifier(parts)
}

func ParseCompoundIdentifier(eci EncodedCompoundIdentifier) CompoundIdentifier {
	parts := strings.SplitN(string(eci), "/", 2)
	raw_library := ""
	raw_name := parts[0]
	if len(parts) == 2 {
		raw_library = parts[0]
		raw_name = parts[1]
	}
	library := ParseLibraryName(EncodedLibraryIdentifier(raw_library))
	name_parts := strings.SplitN(raw_name, ".", 2)
	name := Identifier(name_parts[0])
	member := Identifier("")
	if len(name_parts) == 2 {
		member = Identifier(name_parts[1])
	}
	return CompoundIdentifier{library, name, member}
}

func EnsureLibrary(l EncodedLibraryIdentifier, eci EncodedCompoundIdentifier) EncodedCompoundIdentifier {
	if strings.Index(string(eci), "/") != -1 {
		return eci
	}
	new_eci := strings.Join([]string{string(l), "/", string(eci)}, "")
	return EncodedCompoundIdentifier(new_eci)
}

type PrimitiveSubtype string

const (
	Bool    PrimitiveSubtype = "bool"
	Int8    PrimitiveSubtype = "int8"
	Int16   PrimitiveSubtype = "int16"
	Int32   PrimitiveSubtype = "int32"
	Int64   PrimitiveSubtype = "int64"
	Uint8   PrimitiveSubtype = "uint8"
	Uint16  PrimitiveSubtype = "uint16"
	Uint32  PrimitiveSubtype = "uint32"
	Uint64  PrimitiveSubtype = "uint64"
	Float32 PrimitiveSubtype = "float32"
	Float64 PrimitiveSubtype = "float64"
)

var unsignedSubtypes = map[PrimitiveSubtype]struct{}{
	Uint8:  {},
	Uint16: {},
	Uint32: {},
	Uint64: {},
}

// IsSigned indicates whether this subtype represents a signed number such as
// `int16`, or `float32`.
func (typ PrimitiveSubtype) IsSigned() bool {
	return !typ.IsUnsigned()
}

// IsUnsigned indicates whether this subtype represents an unsigned number such
// as `uint16`.
func (typ PrimitiveSubtype) IsUnsigned() bool {
	_, ok := unsignedSubtypes[typ]
	return ok
}

type HandleSubtype string

const (
	Handle       HandleSubtype = "handle"
	Bti          HandleSubtype = "bti"
	Channel      HandleSubtype = "channel"
	Clock        HandleSubtype = "clock"
	DebugLog     HandleSubtype = "debuglog"
	Event        HandleSubtype = "event"
	Eventpair    HandleSubtype = "eventpair"
	Exception    HandleSubtype = "exception"
	Fifo         HandleSubtype = "fifo"
	Guest        HandleSubtype = "guest"
	Interrupt    HandleSubtype = "interrupt"
	Iommu        HandleSubtype = "iommu"
	Job          HandleSubtype = "job"
	Pager        HandleSubtype = "pager"
	PciDevice    HandleSubtype = "pcidevice"
	Pmt          HandleSubtype = "pmt"
	Port         HandleSubtype = "port"
	Process      HandleSubtype = "process"
	Profile      HandleSubtype = "profile"
	Resource     HandleSubtype = "resource"
	Socket       HandleSubtype = "socket"
	Stream       HandleSubtype = "stream"
	SuspendToken HandleSubtype = "suspendtoken"
	Thread       HandleSubtype = "thread"
	Time         HandleSubtype = "timer"
	Vcpu         HandleSubtype = "vcpu"
	Vmar         HandleSubtype = "vmar"
	Vmo          HandleSubtype = "vmo"
)

// Copied from third_party/go/src/syscall/zx/types.go
type ObjectType uint32 // zx_obj_type_t
const (
	ObjectTypeNone = ObjectType(iota)
	ObjectTypeProcess
	ObjectTypeThread
	ObjectTypeVmo
	ObjectTypeChannel
	ObjectTypeEvent
	ObjectTypePort
	_ // 7
	_ // 8
	ObjectTypeInterrupt
	_ // 10
	ObjectTypePciDevice
	ObjectTypeLog
	_ // 13
	ObjectTypeSocket
	ObjectTypeResource
	ObjectTypeEventPair
	ObjectTypeJob
	ObjectTypeVmar
	ObjectTypeFifo
	ObjectTypeGuest
	ObjectTypeVcpu
	ObjectTypeTimer
	ObjectTypeIommu
	ObjectTypeBti
	ObjectTypeProfile
	ObjectTypePmt
	ObjectTypeSuspendToken
	ObjectTypePager
)

// TODO(fxbug.dev/45998): emit the numeric value of the subtype in fidlc
func ObjectTypeFromHandleSubtype(val HandleSubtype) ObjectType {
	switch val {
	case Bti:
		return ObjectTypeBti
	case Channel:
		return ObjectTypeChannel
	case DebugLog:
		return ObjectTypeLog
	case Event:
		return ObjectTypeEvent
	case Eventpair:
		return ObjectTypeEventPair
	case Fifo:
		return ObjectTypeFifo
	case Guest:
		return ObjectTypeGuest
	case Interrupt:
		return ObjectTypeInterrupt
	case Iommu:
		return ObjectTypeIommu
	case Job:
		return ObjectTypeJob
	case Pager:
		return ObjectTypePager
	case PciDevice:
		return ObjectTypePciDevice
	case Pmt:
		return ObjectTypePmt
	case Port:
		return ObjectTypePort
	case Process:
		return ObjectTypeProcess
	case Profile:
		return ObjectTypeProfile
	case Resource:
		return ObjectTypeResource
	case Socket:
		return ObjectTypeSocket
	case SuspendToken:
		return ObjectTypeSuspendToken
	case Thread:
		return ObjectTypeThread
	case Time:
		return ObjectTypeTimer
	case Vcpu:
		return ObjectTypeVcpu
	case Vmar:
		return ObjectTypeVmar
	case Vmo:
		return ObjectTypeVmo
	default:
		return ObjectTypeNone
	}
}

type HandleRights uint32

type LiteralKind string

const (
	StringLiteral  LiteralKind = "string"
	NumericLiteral LiteralKind = "numeric"
	TrueLiteral    LiteralKind = "true"
	FalseLiteral   LiteralKind = "false"
	DefaultLiteral LiteralKind = "default"
)

type Literal struct {
	Kind  LiteralKind `json:"kind"`
	Value string      `json:"value,omitempty"`
}

type ConstantKind string

const (
	IdentifierConstant ConstantKind = "identifier"
	LiteralConstant    ConstantKind = "literal"
)

type Constant struct {
	Kind       ConstantKind              `json:"kind"`
	Identifier EncodedCompoundIdentifier `json:"identifier,omitempty"`
	Literal    Literal                   `json:"literal,omitempty"`
}

type TypeKind string

const (
	ArrayType      TypeKind = "array"
	VectorType     TypeKind = "vector"
	StringType     TypeKind = "string"
	HandleType     TypeKind = "handle"
	RequestType    TypeKind = "request"
	PrimitiveType  TypeKind = "primitive"
	IdentifierType TypeKind = "identifier"
)

type Type struct {
	Kind             TypeKind
	ElementType      *Type
	ElementCount     *int
	HandleSubtype    HandleSubtype
	HandleRights     HandleRights
	RequestSubtype   EncodedCompoundIdentifier
	PrimitiveSubtype PrimitiveSubtype
	Identifier       EncodedCompoundIdentifier
	Nullable         bool
}

// UnmarshalJSON customizes the JSON unmarshalling for Type.
func (t *Type) UnmarshalJSON(b []byte) error {
	var obj map[string]*json.RawMessage
	err := json.Unmarshal(b, &obj)
	if err != nil {
		return err
	}

	err = json.Unmarshal(*obj["kind"], &t.Kind)
	if err != nil {
		return err
	}

	switch t.Kind {
	case ArrayType:
		t.ElementType = &Type{}
		err = json.Unmarshal(*obj["element_type"], t.ElementType)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["element_count"], &t.ElementCount)
		if err != nil {
			return err
		}
	case VectorType:
		t.ElementType = &Type{}
		err = json.Unmarshal(*obj["element_type"], t.ElementType)
		if err != nil {
			return err
		}
		if elementCount, ok := obj["maybe_element_count"]; ok {
			err = json.Unmarshal(*elementCount, &t.ElementCount)
			if err != nil {
				return err
			}
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case StringType:
		if elementCount, ok := obj["maybe_element_count"]; ok {
			err = json.Unmarshal(*elementCount, &t.ElementCount)
			if err != nil {
				return err
			}
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case HandleType:
		err = json.Unmarshal(*obj["subtype"], &t.HandleSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["rights"], &t.HandleRights)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case RequestType:
		err = json.Unmarshal(*obj["subtype"], &t.RequestSubtype)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	case PrimitiveType:
		err = json.Unmarshal(*obj["subtype"], &t.PrimitiveSubtype)
		if err != nil {
			return err
		}
	case IdentifierType:
		err = json.Unmarshal(*obj["identifier"], &t.Identifier)
		if err != nil {
			return err
		}
		err = json.Unmarshal(*obj["nullable"], &t.Nullable)
		if err != nil {
			return err
		}
	default:
		return fmt.Errorf("Unknown type kind: %s", t.Kind)
	}

	return nil
}

type Attribute struct {
	Name  Identifier `json:"name"`
	Value string     `json:"value"`
}

// Attributes represents a list of attributes. It conveniently implements the
// `Annotated` protocol, such that it can be embedded into other node structs
// which are annotated.
type Attributes struct {
	Attributes []Attribute `json:"maybe_attributes,omitempty"`
}

func (el Attributes) LookupAttribute(name Identifier) (Attribute, bool) {
	for _, a := range el.Attributes {
		if a.Name == name {
			return a, true
		}
	}
	return Attribute{}, false
}

func (el Attributes) HasAttribute(name Identifier) bool {
	_, ok := el.LookupAttribute(name)
	return ok
}

func (el Attributes) GetAttribute(name Identifier) Attribute {
	attr, _ := el.LookupAttribute(name)
	return attr
}

func (el Attributes) DocComments() []string {
	doc, ok := el.LookupAttribute("Doc")
	if !ok || doc.Value == "" {
		return nil
	}
	return strings.Split(doc.Value[0:len(doc.Value)-1], "\n")
}

func (el Attributes) Transports() map[string]struct{} {
	transports := make(map[string]struct{})
	raw, ok := el.LookupAttribute("Transport")
	if ok && raw.Value != "" {
		for _, transport := range strings.Split(raw.Value, ",") {
			transports[strings.TrimSpace(transport)] = struct{}{}
		}
	}
	// No transport attribute => just Channel
	if !ok {
		transports["Channel"] = struct{}{}
	}
	return transports
}

// BindingsDenylistIncludes returns true if the comma-separated BindingsDenyList
// attribute includes targetLanguage (meaning the bindings for targetLanguage
// should not emit this declaration).
func (el Attributes) BindingsDenylistIncludes(targetLanguage string) bool {
	raw, ok := el.LookupAttribute("BindingsDenylist")
	if ok && raw.Value != "" {
		for _, language := range strings.Split(raw.Value, ",") {
			if strings.TrimSpace(language) == targetLanguage {
				return true
			}
		}
	}
	return false
}

// TypeShape represents the shape of the type on the wire.
// See JSON IR schema, e.g. fidlc --json-schema
type TypeShape struct {
	InlineSize          int  `json:"inline_size"`
	Alignment           int  `json:"alignment"`
	Depth               int  `json:"depth"`
	MaxHandles          int  `json:"max_handles"`
	MaxOutOfLine        int  `json:"max_out_of_line"`
	HasPadding          bool `json:"has_padding"`
	HasFlexibleEnvelope bool `json:"has_flexible_envelope"`
	IsResource          bool `json:"is_resource"`
}

// FieldShape represents the shape of the field on the wire.
// See JSON IR schema, e.g. fidlc --json-schema
type FieldShape struct {
	Offset  int `json:"offset"`
	Padding int `json:"padding"`
}

// Union represents the declaration of a FIDL union.
type Union struct {
	Attributes
	Name        EncodedCompoundIdentifier `json:"name"`
	Members     []UnionMember             `json:"members"`
	Strictness  `json:"strict"`
	TypeShapeV1 TypeShape `json:"type_shape_v1"`
}

// UnionMember represents the declaration of a field in a FIDL extensible
// union.
type UnionMember struct {
	Attributes
	Reserved     bool       `json:"reserved"`
	Ordinal      int        `json:"ordinal"`
	Type         Type       `json:"type"`
	Name         Identifier `json:"name"`
	Offset       int        `json:"offset"`
	MaxOutOfLine int        `json:"max_out_of_line"`
}

// Table represents a declaration of a FIDL table.
type Table struct {
	Attributes
	Name        EncodedCompoundIdentifier `json:"name"`
	Members     []TableMember             `json:"members"`
	TypeShapeV1 TypeShape                 `json:"type_shape_v1"`
}

// TableMember represents the declaration of a field in a FIDL table.
type TableMember struct {
	Attributes
	Reserved          bool       `json:"reserved"`
	Type              Type       `json:"type"`
	Name              Identifier `json:"name"`
	Ordinal           int        `json:"ordinal"`
	MaybeDefaultValue *Constant  `json:"maybe_default_value,omitempty"`
	MaxOutOfLine      int        `json:"max_out_of_line"`
}

// byTableOrdinal is a wrapper type for sorting a []TableMember.
type byTableOrdinal []TableMember

func (s byTableOrdinal) Len() int {
	return len(s)
}

func (s byTableOrdinal) Less(i, j int) bool {
	return s[i].Ordinal < s[j].Ordinal
}

func (s byTableOrdinal) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// SortedMembersNoReserved returns the table's members sorted by ordinal,
// excluding reserved members.
func (t *Table) SortedMembersNoReserved() []TableMember {
	var members []TableMember
	for _, member := range t.Members {
		if !member.Reserved {
			members = append(members, member)
		}
	}
	sort.Sort(byTableOrdinal(members))
	return members
}

// Struct represents a declaration of a FIDL struct.
type Struct struct {
	Attributes
	Name        EncodedCompoundIdentifier `json:"name"`
	Anonymous   bool                      `json:"anonymous"`
	Members     []StructMember            `json:"members"`
	TypeShapeV1 TypeShape                 `json:"type_shape_v1"`
}

// StructMember represents the declaration of a field in a FIDL struct.
type StructMember struct {
	Attributes
	Type              Type       `json:"type"`
	Name              Identifier `json:"name"`
	MaybeDefaultValue *Constant  `json:"maybe_default_value,omitempty"`
	MaxHandles        int        `json:"max_handles"`
	FieldShapeV1      FieldShape `json:"field_shape_v1"`
}

// EmptyStructMember returns a StructMember that's suitable as the sole member
// of an empty struct.
func EmptyStructMember(name string) StructMember {
	// Empty structs have a size of 1, so the uint8 struct member returned by this
	// function can be used to pad the struct to the correct size.

	return StructMember{
		Type: Type{
			Kind:             PrimitiveType,
			PrimitiveSubtype: Uint8,
		},
		Name: Identifier(name),
		MaybeDefaultValue: &Constant{
			Kind:       "literal",
			Identifier: "",
			Literal: Literal{
				Kind:  "numeric",
				Value: "0",
			},
		},
	}
}

// Protocol represents the declaration of a FIDL protocol.
type Protocol struct {
	Attributes
	Name    EncodedCompoundIdentifier `json:"name"`
	Methods []Method                  `json:"methods"`
}

// TODO(fxbug.dev/50195): This is for backward compatibility with fidlgen_dart in
// Topaz, and should be removed after fidlgen_dart code has been updated.
type Interface = Protocol

func (d *Protocol) GetServiceName() string {
	_, found := d.LookupAttribute("Discoverable")
	if found {
		ci := ParseCompoundIdentifier(d.Name)
		var parts []string
		for _, i := range ci.Library {
			parts = append(parts, string(i))
		}
		parts = append(parts, string(ci.Name))
		return fmt.Sprintf("\"%s\"", strings.Join(parts, "."))
	}
	return ""
}

// Service represents the declaration of a FIDL service.
type Service struct {
	Attributes
	Name    EncodedCompoundIdentifier `json:"name"`
	Members []ServiceMember           `json:"members"`
}

func (s *Service) GetServiceName() string {
	ci := ParseCompoundIdentifier(s.Name)
	var parts []string
	for _, i := range ci.Library {
		parts = append(parts, string(i))
	}
	parts = append(parts, string(ci.Name))
	return strings.Join(parts, ".")
}

// ServiceMember represents the declaration of a field in a FIDL service.
type ServiceMember struct {
	Attributes
	Name Identifier `json:"name"`
	Type Type       `json:"type"`
}

// Method represents the declaration of a FIDL method.
type Method struct {
	Attributes
	Ordinal             uint64                    `json:"ordinal"`
	Name                Identifier                `json:"name"`
	HasRequest          bool                      `json:"has_request"`
	Request             []Parameter               `json:"maybe_request,omitempty"`
	RequestPayload      EncodedCompoundIdentifier `json:"maybe_request_payload,omitempty"`
	RequestTypeShapeV1  TypeShape                 `json:"maybe_request_type_shape_v1,omitempty"`
	RequestPadding      bool                      `json:"maybe_request_has_padding,omitempty"`
	RequestFlexible     bool                      `json:"experimental_maybe_request_has_flexible_envelope,omitempty"`
	HasResponse         bool                      `json:"has_response"`
	Response            []Parameter               `json:"maybe_response,omitempty"`
	ResponsePayload     EncodedCompoundIdentifier `json:"maybe_response_payload,omitempty"`
	ResponseTypeShapeV1 TypeShape                 `json:"maybe_response_type_shape_v1,omitempty"`
	ResponsePadding     bool                      `json:"maybe_response_has_padding,omitempty"`
	ResponseFlexible    bool                      `json:"experimental_maybe_response_has_flexible_envelope,omitempty"`
}

// IsTransitional returns whether this method has the `Transitional` attribute.
func (m *Method) IsTransitional() bool {
	_, transitional := m.LookupAttribute("Transitional")
	return transitional
}

// Parameter represents a parameter to a FIDL method.
type Parameter struct {
	Type         Type       `json:"type"`
	Name         Identifier `json:"name"`
	MaxHandles   int        `json:"max_handles"`
	MaxOutOfLine int        `json:"max_out_of_line"`
	FieldShapeV1 FieldShape `json:"field_shape_v1"`
}

// Enum represents a FIDL declaration of an enum.
type Enum struct {
	Attributes
	Type            PrimitiveSubtype          `json:"type"`
	Name            EncodedCompoundIdentifier `json:"name"`
	Members         []EnumMember              `json:"members"`
	Strictness      `json:"strict"`
	RawUnknownValue int64OrUint64 `json:"maybe_unknown_value"`
}

// UnknownValueAsInt64 retrieves the unknown value. Succeeds only for signed
// flexible enums.
func (enum *Enum) UnknownValueAsInt64() (int64, error) {
	if enum.IsStrict() {
		return 0, fmt.Errorf("cannot retrieve unknown value of strict enum")
	}
	if enum.Type.IsUnsigned() {
		return 0, fmt.Errorf("cannot retrieve signed unknown value of unsigned flexible enum")
	}
	return enum.RawUnknownValue.readInt64(), nil
}

// UnknownValueAsUint64 retrieves the unknown value. Succeeds only for unsigned
// flexible enums.
func (enum *Enum) UnknownValueAsUint64() (uint64, error) {
	if enum.IsStrict() {
		return 0, fmt.Errorf("cannot retrieve unknown value of strict enum")
	}
	if enum.Type.IsSigned() {
		return 0, fmt.Errorf("cannot retrieve unsigned unknown value of signed flexible enum")
	}
	return enum.RawUnknownValue.readUint64(), nil
}

// UnknownValueForTmpl retrieves the signed or unsigned unknown value. Panics
// if called on a strict enum.
func (enum *Enum) UnknownValueForTmpl() interface{} {
	if enum.Type.IsSigned() {
		unknownValue, err := enum.UnknownValueAsInt64()
		if err != nil {
			panic(err.Error())
		}
		return unknownValue
	}

	unknownValue, err := enum.UnknownValueAsUint64()
	if err != nil {
		panic(err.Error())
	}
	return unknownValue
}

// EnumMember represents a single variant in a FIDL enum.
type EnumMember struct {
	Attributes
	Name  Identifier `json:"name"`
	Value Constant   `json:"value"`
}

// IsUnknown indicates whether this member represents a custom unknown flexible
// enum member.
func (member *EnumMember) IsUnknown() bool {
	return member.HasAttribute("Unknown")
}

// Bits represents a FIDL declaration of an bits.
type Bits struct {
	Attributes
	Type       Type                      `json:"type"`
	Name       EncodedCompoundIdentifier `json:"name"`
	Mask       string                    `json:"mask"`
	Members    []BitsMember              `json:"members"`
	Strictness `json:"strict"`
}

// BitsMember represents a single variant in a FIDL bits.
type BitsMember struct {
	Attributes
	Name  Identifier `json:"name"`
	Value Constant   `json:"value"`
}

// Const represents a FIDL declaration of a named constant.
type Const struct {
	Attributes
	Type  Type                      `json:"type"`
	Name  EncodedCompoundIdentifier `json:"name"`
	Value Constant                  `json:"value"`
}

// Strictness represents whether a FIDL object is strict or flexible. See
// <https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-033> for more
// information.
type Strictness bool

const (
	IsFlexible Strictness = false
	IsStrict   Strictness = true
)

// IsStrict indicates whether this type is strict.
func (s Strictness) IsStrict() bool {
	return s == IsStrict
}

// IsFlexible indicates whether this type is flexible.
func (s Strictness) IsFlexible() bool {
	return s == IsFlexible
}

type DeclType string

const (
	ConstDeclType    DeclType = "const"
	BitsDeclType     DeclType = "bits"
	EnumDeclType     DeclType = "enum"
	ProtocolDeclType DeclType = "interface"
	ServiceDeclType  DeclType = "service"
	StructDeclType   DeclType = "struct"
	TableDeclType    DeclType = "table"
	UnionDeclType    DeclType = "union"

	// TODO(fxbug.dev/50195): This is for backward compatibility with fidlgen_dart in
	// Topaz, and should be removed after fidlgen_dart code has been updated.
	InterfaceDeclType DeclType = "interface"
)

type DeclMap map[EncodedCompoundIdentifier]DeclType

func (dt DeclType) IsPrimitive() bool {
	switch dt {
	case BitsDeclType, EnumDeclType:
		return true
	}

	return false
}

// Library represents a FIDL dependency on a separate library.
type Library struct {
	Name  EncodedLibraryIdentifier `json:"name,omitempty"`
	Decls DeclMap                  `json:"declarations,omitempty"`
}

// Root is the top-level object for a FIDL library.
// It contains lists of all declarations and dependencies within the library.
type Root struct {
	Name      EncodedLibraryIdentifier    `json:"name,omitempty"`
	Consts    []Const                     `json:"const_declarations,omitempty"`
	Bits      []Bits                      `json:"bits_declarations,omitempty"`
	Enums     []Enum                      `json:"enum_declarations,omitempty"`
	Protocols []Protocol                  `json:"interface_declarations,omitempty"`
	Services  []Service                   `json:"service_declarations,omitempty"`
	Structs   []Struct                    `json:"struct_declarations,omitempty"`
	Tables    []Table                     `json:"table_declarations,omitempty"`
	Unions    []Union                     `json:"union_declarations,omitempty"`
	DeclOrder []EncodedCompoundIdentifier `json:"declaration_order,omitempty"`
	Decls     DeclMap                     `json:"declarations,omitempty"`
	Libraries []Library                   `json:"library_dependencies,omitempty"`

	// TODO(fxbug.dev/50195): This is for backward compatibility with fidlgen_dart in
	// Topaz, and should be removed after fidlgen_dart code has been updated.
	Interfaces []Protocol
}

// DeclsWithDependencies returns a single DeclMap containing the FIDL
// library's declarations and those of its dependencies.
func (r *Root) DeclsWithDependencies() DeclMap {
	decls := DeclMap{}
	for k, v := range r.Decls {
		decls[k] = v
	}
	for _, l := range r.Libraries {
		for k, v := range l.Decls {
			decls[EnsureLibrary(l.Name, k)] = v
		}
	}
	return decls
}

// ForBindings filters out declarations that should be omitted in the given
// language bindings based on BindingsDenylist attributes. It returns a new Root
// and does not modify r.
func (r *Root) ForBindings(language string) Root {
	res := Root{
		Name:      r.Name,
		Libraries: r.Libraries,
		Decls:     make(DeclMap, len(r.Decls)),
	}

	for _, v := range r.Consts {
		if !v.BindingsDenylistIncludes(language) {
			res.Consts = append(res.Consts, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Bits {
		if !v.BindingsDenylistIncludes(language) {
			res.Bits = append(res.Bits, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Enums {
		if !v.BindingsDenylistIncludes(language) {
			res.Enums = append(res.Enums, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Protocols {
		if !v.BindingsDenylistIncludes(language) {
			res.Protocols = append(res.Protocols, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Services {
		if !v.BindingsDenylistIncludes(language) {
			res.Services = append(res.Services, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Structs {
		if !v.BindingsDenylistIncludes(language) {
			res.Structs = append(res.Structs, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Tables {
		if !v.BindingsDenylistIncludes(language) {
			res.Tables = append(res.Tables, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Unions {
		if !v.BindingsDenylistIncludes(language) {
			res.Unions = append(res.Unions, v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}

	for _, d := range r.DeclOrder {
		if _, ok := res.Decls[d]; ok {
			res.DeclOrder = append(res.DeclOrder, d)
		}
	}

	return res
}

type int64OrUint64 struct {
	i int64
	u uint64
}

func (n *int64OrUint64) readInt64() int64 {
	if n.i != 0 {
		return n.i
	}
	return int64(n.u)
}

func (n *int64OrUint64) readUint64() uint64 {
	if n.i != 0 {
		return uint64(n.i)
	}
	return n.u
}

var _ json.Unmarshaler = (*int64OrUint64)(nil)

func (n *int64OrUint64) UnmarshalJSON(data []byte) error {
	if u, err := strconv.ParseUint(string(data), 10, 64); err == nil {
		n.u = u
		return nil
	}
	if i, err := strconv.ParseInt(string(data), 10, 64); err == nil {
		n.i = i
		return nil
	}
	return fmt.Errorf("%s not representable as int64 or uint64", string(data))
}
