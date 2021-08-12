// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
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
	f, err := os.Open(filename)
	if err != nil {
		return Root{}, fmt.Errorf("Error reading from %s: %w", filename, err)
	}
	return DecodeJSONIr(f)
}

// DecodeJSONIr reads the JSON content from a reader.
func DecodeJSONIr(r io.Reader) (Root, error) {
	d := json.NewDecoder(r)
	var root Root
	if err := d.Decode(&root); err != nil {
		return Root{}, fmt.Errorf("Error parsing JSON IR: %w", err)
	}

	root.initializeDeclarationsMap()
	root.initializeMethodResults()

	return root, nil
}

// ReadJSONIrContent reads JSON IR content.
func ReadJSONIrContent(b []byte) (Root, error) {
	return DecodeJSONIr(bytes.NewReader(b))
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

func (li LibraryIdentifier) Encode() EncodedLibraryIdentifier {
	ss := make([]string, len(li))
	for i, s := range li {
		ss[i] = string(s)
	}
	return EncodedLibraryIdentifier(strings.Join(ss, "."))
}

func (ci CompoundIdentifier) EncodeDecl() EncodedCompoundIdentifier {
	return EncodedCompoundIdentifier(string(ci.Library.Encode()) + "/" + string(ci.Name))
}

func (ci CompoundIdentifier) Encode() EncodedCompoundIdentifier {
	if ci.Member != "" {
		return EncodedCompoundIdentifier(fmt.Sprintf("%s.%s", ci.EncodeDecl(), ci.Member))
	}
	return ci.EncodeDecl()
}

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

func (eci EncodedCompoundIdentifier) DeclName() EncodedCompoundIdentifier {
	ci := ParseCompoundIdentifier(eci)
	parts := []string{}
	for _, l := range ci.Library {
		parts = append(parts, string(l))
	}
	return EncodedCompoundIdentifier(fmt.Sprintf("%s/%s",
		strings.Join(parts, "."), ci.Name))
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

// TODO(fxb/64629): Remove, source of truth is library zx.
//
// One complication is that GIDL parses nice handle subtypes in its grammar,
// e.g. `#0 = event(rights: execute + write )`. And some GIDL backends care
// about the object type. This means that we need to duplicate this mapping :/
// It would be cleaner to limit this to GIDL and GIDL backends, rather than
// offer that in the general purpose lib/fidlgen.
type ObjectType uint32

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
	ObjectTypeException
	ObjectTypeClock
)

func ObjectTypeFromHandleSubtype(val HandleSubtype) ObjectType {
	switch val {
	case Bti:
		return ObjectTypeBti
	case Channel:
		return ObjectTypeChannel
	case Clock:
		return ObjectTypeClock
	case DebugLog:
		return ObjectTypeLog
	case Event:
		return ObjectTypeEvent
	case Eventpair:
		return ObjectTypeEventPair
	case Exception:
		return ObjectTypeException
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

const (
	HandleRightsNone HandleRights = 0

	HandleRightsDuplicate     HandleRights = 1 << 0
	HandleRightsTransfer      HandleRights = 1 << 1
	HandleRightsRead          HandleRights = 1 << 2
	HandleRightsWrite         HandleRights = 1 << 3
	HandleRightsExecute       HandleRights = 1 << 4
	HandleRightsMap           HandleRights = 1 << 5
	HandleRightsGetProperty   HandleRights = 1 << 6
	HandleRightsSetProperty   HandleRights = 1 << 7
	HandleRightsEnumerate     HandleRights = 1 << 8
	HandleRightsDestroy       HandleRights = 1 << 9
	HandleRightsSetPolicy     HandleRights = 1 << 10
	HandleRightsGetPolicy     HandleRights = 1 << 11
	HandleRightsSignal        HandleRights = 1 << 12
	HandleRightsSignalPeer    HandleRights = 1 << 13
	HandleRightsWait          HandleRights = 1 << 14
	HandleRightsInspect       HandleRights = 1 << 15
	HandleRightsManageJob     HandleRights = 1 << 16
	HandleRightsManageProcess HandleRights = 1 << 17
	HandleRightsManageThread  HandleRights = 1 << 18
	HandleRightsApplyProfile  HandleRights = 1 << 19

	HandleRightsSameRights HandleRights = 1 << 31

	HandleRightsBasic HandleRights = HandleRightsTransfer | HandleRightsDuplicate | HandleRightsWait | HandleRightsInspect
)

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
	BinaryOperator     ConstantKind = "binary_operator"
)

type Constant struct {
	Kind       ConstantKind              `json:"kind"`
	Identifier EncodedCompoundIdentifier `json:"identifier,omitempty"`
	Literal    Literal                   `json:"literal,omitempty"`
	Value      string                    `json:"value"`
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
	ObjType          uint32
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
		err = json.Unmarshal(*obj["obj_type"], &t.ObjType)
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
		if ToSnakeCase(string(a.Name)) == ToSnakeCase(string(name)) {
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
	doc, ok := el.LookupAttribute("doc")
	if !ok || doc.Value == "" {
		return nil
	}
	return strings.Split(doc.Value[0:len(doc.Value)-1], "\n")
}

func (el Attributes) Transports() map[string]struct{} {
	transports := make(map[string]struct{})
	raw, ok := el.LookupAttribute("transport")
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

// BindingsDenylistIncludes returns true if the comma-separated
// bindings_denylist attribute includes targetLanguage (meaning the bindings for
// targetLanguage should not emit this declaration).
func (el Attributes) BindingsDenylistIncludes(targetLanguage string) bool {
	raw, ok := el.LookupAttribute("bindings_denylist")
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
}

// FieldShape represents the shape of the field on the wire.
// See JSON IR schema, e.g. fidlc --json-schema
type FieldShape struct {
	Offset  int `json:"offset"`
	Padding int `json:"padding"`
}

type Declaration interface {
	GetName() EncodedCompoundIdentifier
}

type Decl struct {
	Attributes
	Name EncodedCompoundIdentifier `json:"name"`
}

func (d *Decl) GetName() EncodedCompoundIdentifier {
	return d.Name
}

// Layout represents data specific to bits/enums/structs/tables/unions. All
// layouts are decls, but not all decls are layouts (e.g. protocols).
type Layout struct {
	Decl
	NamingContext []string `json:"naming_context"`
}

func (l *Layout) IsAnonymous() bool {
	// We treat inner layouts (i.e. layouts defined within another layout) as
	// anonymous. All such layouts have a naming context with length greater
	// than 1, since they include at least the top level name followed by 1 or
	// more inner names
	return len(l.NamingContext) > 1
}

// Assert that declarations conform to the Declaration interface
var _ = []Declaration{
	(*Union)(nil),
	(*Table)(nil),
	(*Struct)(nil),
	(*Protocol)(nil),
	(*Service)(nil),
	(*Enum)(nil),
	(*Bits)(nil),
	(*Const)(nil),
}

// Union represents the declaration of a FIDL union.
type Union struct {
	Layout
	Members      []UnionMember `json:"members"`
	Strictness   `json:"strict"`
	Resourceness `json:"resource"`
	TypeShapeV1  TypeShape `json:"type_shape_v1"`
	TypeShapeV2  TypeShape `json:"type_shape_v2"`

	MethodResult *MethodResult
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
	Layout
	Members      []TableMember `json:"members"`
	Resourceness `json:"resource"`
	TypeShapeV1  TypeShape `json:"type_shape_v1"`
	TypeShapeV2  TypeShape `json:"type_shape_v2"`
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
	Layout
	IsRequestOrResponse bool           `json:"is_request_or_response"`
	Members             []StructMember `json:"members"`
	Resourceness        `json:"resource"`
	TypeShapeV1         TypeShape `json:"type_shape_v1"`
	TypeShapeV2         TypeShape `json:"type_shape_v2"`

	MethodResult *MethodResult
}

// StructMember represents the declaration of a field in a FIDL struct.
type StructMember struct {
	Attributes
	Type              Type       `json:"type"`
	Name              Identifier `json:"name"`
	MaybeDefaultValue *Constant  `json:"maybe_default_value,omitempty"`
	MaxHandles        int        `json:"max_handles"`
	FieldShapeV1      FieldShape `json:"field_shape_v1"`
	FieldShapeV2      FieldShape `json:"field_shape_v2"`
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
	Decl
	Methods []Method `json:"methods"`
}

func (d *Protocol) GetServiceName() string {
	_, found := d.LookupAttribute("discoverable")
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
	Decl
	Members []ServiceMember `json:"members"`
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
	RequestTypeShapeV2  TypeShape                 `json:"maybe_request_type_shape_v2,omitempty"`
	RequestPadding      bool                      `json:"maybe_request_has_padding,omitempty"`
	RequestFlexible     bool                      `json:"experimental_maybe_request_has_flexible_envelope,omitempty"`
	HasResponse         bool                      `json:"has_response"`
	Response            []Parameter               `json:"maybe_response,omitempty"`
	ResponsePayload     EncodedCompoundIdentifier `json:"maybe_response_payload,omitempty"`
	ResponseTypeShapeV1 TypeShape                 `json:"maybe_response_type_shape_v1,omitempty"`
	ResponseTypeShapeV2 TypeShape                 `json:"maybe_response_type_shape_v2,omitempty"`
	ResponsePadding     bool                      `json:"maybe_response_has_padding,omitempty"`
	ResponseFlexible    bool                      `json:"experimental_maybe_response_has_flexible_envelope,omitempty"`

	MethodResult *MethodResult
}

// IsTransitional returns whether this method has the `Transitional` attribute.
func (m *Method) IsTransitional() bool {
	_, transitional := m.LookupAttribute("transitional")
	return transitional
}

// MethodResult represents how FIDL methods that use the error syntax are manifested in the IR.
type MethodResult struct {
	Method      *Method
	ResultType  Type
	ResultUnion *Union
	ValueMember *UnionMember
	ValueType   Type
	ValueStruct *Struct
	ErrorMember *UnionMember
	ErrorType   Type
}

func (m *Method) calculateMethodResult(r *Root) *MethodResult {
	var mr = MethodResult{Method: m}

	// Methods with a result have a single response parameter.
	if !m.HasResponse || len(m.Response) != 1 {
		return nil
	}
	mr.ResultType = m.Response[0].Type

	// Result response parameters are always unions
	if mr.ResultType.Kind != IdentifierType ||
		r.Decls[mr.ResultType.Identifier] != UnionDeclType {
		return nil
	}

	// Find the result union
	mr.ResultUnion, _ = r.LookupDecl(mr.ResultType.Identifier).(*Union)
	if !mr.ResultUnion.HasAttribute("Result") {
		// After all that it's not even a result union.
		return nil
	}

	mr.ValueMember = &mr.ResultUnion.Members[0]
	mr.ValueType = mr.ValueMember.Type
	mr.ValueStruct = r.LookupDecl(mr.ValueType.Identifier).(*Struct)
	mr.ErrorMember = &mr.ResultUnion.Members[1]
	mr.ErrorType = mr.ErrorMember.Type

	return &mr
}

// Parameter represents a parameter to a FIDL method.
type Parameter struct {
	Type         Type       `json:"type"`
	Name         Identifier `json:"name"`
	MaxHandles   int        `json:"max_handles"`
	MaxOutOfLine int        `json:"max_out_of_line"`
	FieldShapeV1 FieldShape `json:"field_shape_v1"`
	FieldShapeV2 FieldShape `json:"field_shape_v2"`
}

// Enum represents a FIDL declaration of an enum.
type Enum struct {
	Layout
	Type            PrimitiveSubtype `json:"type"`
	Members         []EnumMember     `json:"members"`
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
			panic(err)
		}
		return unknownValue
	}

	unknownValue, err := enum.UnknownValueAsUint64()
	if err != nil {
		panic(err)
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
	Layout
	Type       Type         `json:"type"`
	Mask       string       `json:"mask"`
	Members    []BitsMember `json:"members"`
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
	Decl
	Type  Type     `json:"type"`
	Value Constant `json:"value"`
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

// Resourceness represents whether a FIDL object may contain any resource types,
// such as handles. See https://fuchsia.dev/fuchsia-src/contribute/governance/fidl/ftp/ftp-057
// for more information.
type Resourceness bool

const (
	IsResourceType Resourceness = true
	IsValueType    Resourceness = false
)

// IsResourceType indicates whether this type is marked as a resource type
func (r Resourceness) IsResourceType() bool {
	return r == IsResourceType
}

// IsValueType indicates whether this type is not marked as a resource type
func (r Resourceness) IsValueType() bool {
	return r == IsValueType
}

type DeclType string

const (
	LibraryDeclType DeclType = "library"

	ConstDeclType    DeclType = "const"
	BitsDeclType     DeclType = "bits"
	EnumDeclType     DeclType = "enum"
	ProtocolDeclType DeclType = "interface"
	ServiceDeclType  DeclType = "service"
	StructDeclType   DeclType = "struct"
	TableDeclType    DeclType = "table"
	UnionDeclType    DeclType = "union"
)

type DeclInfo struct {
	Type DeclType `json:"kind"`
	// Present for structs, tables, and unions.
	*Resourceness `json:"resource,omitempty"`
}

type DeclMap map[EncodedCompoundIdentifier]DeclType
type DeclInfoMap map[EncodedCompoundIdentifier]DeclInfo

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
	Decls DeclInfoMap              `json:"declarations,omitempty"`
}

// Root is the top-level object for a FIDL library.
// It contains lists of all declarations and dependencies within the library.
type Root struct {
	Name         EncodedLibraryIdentifier    `json:"name,omitempty"`
	Consts       []Const                     `json:"const_declarations,omitempty"`
	Bits         []Bits                      `json:"bits_declarations,omitempty"`
	Enums        []Enum                      `json:"enum_declarations,omitempty"`
	Protocols    []Protocol                  `json:"interface_declarations,omitempty"`
	Services     []Service                   `json:"service_declarations,omitempty"`
	Structs      []Struct                    `json:"struct_declarations,omitempty"`
	Tables       []Table                     `json:"table_declarations,omitempty"`
	Unions       []Union                     `json:"union_declarations,omitempty"`
	DeclOrder    []EncodedCompoundIdentifier `json:"declaration_order,omitempty"`
	Decls        DeclMap                     `json:"declarations,omitempty"`
	Libraries    []Library                   `json:"library_dependencies,omitempty"`
	declarations map[EncodedCompoundIdentifier]Declaration
}

func (r *Root) initializeDeclarationsMap() {
	r.declarations = make(map[EncodedCompoundIdentifier]Declaration)
	for i, d := range r.Consts {
		r.declarations[d.Name] = &r.Consts[i]
	}
	for i, d := range r.Bits {
		r.declarations[d.Name] = &r.Bits[i]
	}
	for i, d := range r.Enums {
		r.declarations[d.Name] = &r.Enums[i]
	}
	for i, d := range r.Protocols {
		r.declarations[d.Name] = &r.Protocols[i]
	}
	for i, d := range r.Services {
		r.declarations[d.Name] = &r.Services[i]
	}
	for i, d := range r.Structs {
		r.declarations[d.Name] = &r.Structs[i]
	}
	for i, d := range r.Tables {
		r.declarations[d.Name] = &r.Tables[i]
	}
	for i, d := range r.Unions {
		r.declarations[d.Name] = &r.Unions[i]
	}
}

func (r *Root) initializeMethodResults() {
	for p := 0; p < len(r.Protocols); p++ {
		protocol := &r.Protocols[p]
		for m := 0; m < len(protocol.Methods); m++ {
			mr := protocol.Methods[m].calculateMethodResult(r)
			if mr != nil {
				mr.Method.MethodResult = mr
				mr.ResultUnion.MethodResult = mr
				mr.ValueStruct.MethodResult = mr
			}
		}
	}
}

// DeclsWithDependencies returns a single DeclInfoMap containing the FIDL
// library's declarations and those of its dependencies.
func (r *Root) DeclsWithDependencies() DeclInfoMap {
	resourceness := make(map[EncodedCompoundIdentifier]Resourceness, len(r.Structs)+len(r.Tables)+len(r.Unions))
	for _, v := range r.Structs {
		resourceness[v.Name] = v.Resourceness
	}
	for _, v := range r.Tables {
		resourceness[v.Name] = v.Resourceness
	}
	for _, v := range r.Unions {
		resourceness[v.Name] = v.Resourceness
	}
	decls := DeclInfoMap{}
	for k, v := range r.Decls {
		ptr := new(Resourceness)
		*ptr = resourceness[k]
		decls[k] = DeclInfo{Type: v, Resourceness: ptr}
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
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Bits = append(res.Bits, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Enums {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Enums = append(res.Enums, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Protocols {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Methods = nil
			for _, m := range v.Methods {
				if !m.BindingsDenylistIncludes(language) {
					newV.Methods = append(newV.Methods, m)
				}
			}
			res.Protocols = append(res.Protocols, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Services {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Services = append(res.Services, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Structs {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Structs = append(res.Structs, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Tables {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				} else {
					newV.Members = append(newV.Members, TableMember{
						Attributes: m.Attributes,
						Reserved:   true,
						Name:       m.Name,
						Ordinal:    m.Ordinal,
					})
				}
			}
			res.Tables = append(res.Tables, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}
	for _, v := range r.Unions {
		if !v.BindingsDenylistIncludes(language) {
			newV := v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				} else {
					newV.Members = append(newV.Members, UnionMember{
						Attributes: m.Attributes,
						Reserved:   true,
						Name:       m.Name,
						Ordinal:    m.Ordinal,
					})
				}
			}
			res.Unions = append(res.Unions, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	}

	for _, d := range r.DeclOrder {
		if _, ok := res.Decls[d]; ok {
			res.DeclOrder = append(res.DeclOrder, d)
		}
	}

	r.initializeDeclarationsMap()

	return res
}

func (r *Root) LookupDecl(i EncodedCompoundIdentifier) Declaration {
	return r.declarations[i]
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
