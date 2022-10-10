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
	"reflect"
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
	return root, nil
}

// ReadJSONIrContent reads JSON IR content.
func ReadJSONIrContent(b []byte) (Root, error) {
	return DecodeJSONIr(bytes.NewReader(b))
}

type Identifier string

// A LibraryIdentifier identifies a FIDL library, from the library declaration
// at the start of a FIDL file.
type LibraryIdentifier []Identifier

// A CompoundIdentifier identifies a particular declaration in a library or
// member in a declaration.
type CompoundIdentifier struct {
	// Library the declaration is in.
	Library LibraryIdentifier
	// Name of the declaration.
	Name Identifier
	// Member of the declaration. If set to empty string, this
	// CompoundIdentifier refers to the declaration rather than a member.
	Member Identifier
}

// Experiment lists the name of an experiment active on this IR.
type Experiment string

// This is a direct copy of the fidlc experiment flag strings, found at
// ///tools/fidl/fidlc/lib/experimental_flags.cc. That list should be considered
// the source of truth for this mirror.
const (
	ExperimentAllowNewTypes       Experiment = "allow_new_types"
	ExperimentAllowOverflowing    Experiment = "allow_overflowing"
	ExperimentNoOptionalStructs   Experiment = "no_optional_structs"
	ExperimentOutputIndexJSON     Experiment = "output_index_json"
	ExperimentUnknownInteractions Experiment = "unknown_interactions"
)

// Experiments is a list of active experiments for this IR.
type Experiments []Experiment

// Contains checks if a given experiment string is included in the list.
func (exs Experiments) Contains(needle Experiment) bool {
	for _, ex := range exs {
		if needle == ex {
			return true
		}
	}
	return false
}

// An EncodedLibraryIdentifier is a LibraryIdentifier encoded as a string,
// suitable for use in map keys.
type EncodedLibraryIdentifier string

// An EncodedCompoundIdentifier is a CompoundIdentifier encoded as a string,
// suitable for use in map keys.
type EncodedCompoundIdentifier string

// Encode formats a LibraryIdentifier as a string by joining the identifier
// components with ".", e.g.  "my.fidl.library".
func (li LibraryIdentifier) Encode() EncodedLibraryIdentifier {
	ss := make([]string, len(li))
	for i, s := range li {
		ss[i] = string(s)
	}
	return EncodedLibraryIdentifier(strings.Join(ss, "."))
}

// EncodeDecl encodes the fully-qualified declaration portion of the
// CompoundIdentifier.
//
// Encoded form consists of the encoded form of the library identifier, followed
// by a slash, then the name of the declaration. If the CompoundIdentifier does
// not have a Member, this will be the same as Encode. Example:
// "my.fidl.library/MyProtocol".
func (ci CompoundIdentifier) EncodeDecl() EncodedCompoundIdentifier {
	return EncodedCompoundIdentifier(string(ci.Library.Encode()) + "/" + string(ci.Name))
}

// Encode encodes the fully-qualified declaration or member identified by this
// CompoundIdentifier.
//
// Encoded form consists of the encoded library identifier, then the declaration
// name. If a member is specified, it will come after the declaration name,
// separated by a dot. Example:
// - With no Member: "my.fidl.library/MyProtocol"
// - With Member: "my.fidl.library/MyProtocol.SomeMethod"
func (ci CompoundIdentifier) Encode() EncodedCompoundIdentifier {
	if ci.Member != "" {
		return EncodedCompoundIdentifier(fmt.Sprintf("%s.%s", ci.EncodeDecl(), ci.Member))
	}
	return ci.EncodeDecl()
}

// Parts splits the library identifier back into component parts.
func (eli EncodedLibraryIdentifier) Parts() []string {
	return strings.Split(string(eli), ".")
}

// Parse decodes an EncodedLibraryIdentifier back into a LibraryIdentifier.
func (eli EncodedLibraryIdentifier) Parse() LibraryIdentifier {
	parts := eli.Parts()
	idents := make([]Identifier, len(parts))
	for i, part := range parts {
		idents[i] = Identifier(part)
	}
	return LibraryIdentifier(idents)
}

// PartsReversed splits the library identifier back into component parts and
// returns them in reverse order.
func (eli EncodedLibraryIdentifier) PartsReversed() []string {
	parts := eli.Parts()
	partsReversed := make([]string, len(parts))
	for i, part := range parts {
		partsReversed[len(parts)-i-1] = string(part)
	}

	return partsReversed
}

// Parts splits an EncodedCompoundIdentifier into an optional library name and
// declaration or member id.
//
// This splits off the library name, but does not check whether the referenced
// member is a delaration or member of a declaration.
func (eci EncodedCompoundIdentifier) Parts() []string {
	return strings.SplitN(string(eci), "/", 2)
}

// LibraryName retrieves the library name from an EncodedCompoundIdentifier.
func (eci EncodedCompoundIdentifier) LibraryName() EncodedLibraryIdentifier {
	raw_library := ""
	if parts := eci.Parts(); len(parts) == 2 {
		raw_library = parts[0]
	}
	return EncodedLibraryIdentifier(raw_library)
}

// DeclName retrieves the fully-qualified declaration name from an
// EncodedCompoundIdentifier. This operation is idempotent.
func (eci EncodedCompoundIdentifier) DeclName() EncodedCompoundIdentifier {
	ci := eci.Parse()
	parts := []string{}
	for _, l := range ci.Library {
		parts = append(parts, string(l))
	}
	return EncodedCompoundIdentifier(fmt.Sprintf("%s/%s",
		strings.Join(parts, "."), ci.Name))
}

// IsBuiltIn gives whether the identifier corresponds to a built-in type.
func (eci EncodedCompoundIdentifier) IsBuiltIn() bool {
	return eci.LibraryName() == ""
}

// Parse converts an EncodedCompoundIdentifier back into a CompoundIdentifier.
func (eci EncodedCompoundIdentifier) Parse() CompoundIdentifier {
	parts := eci.Parts()
	raw_library := ""
	raw_name := parts[0]
	if len(parts) == 2 {
		raw_library = parts[0]
		raw_name = parts[1]
	}
	library := EncodedLibraryIdentifier(raw_library).Parse()
	name_parts := strings.SplitN(raw_name, ".", 2)
	name := Identifier(name_parts[0])
	member := Identifier("")
	if len(name_parts) == 2 {
		member = Identifier(name_parts[1])
	}
	return CompoundIdentifier{library, name, member}
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

var numberOfBits = map[PrimitiveSubtype]int{
	Bool:    8,
	Int8:    8,
	Int16:   16,
	Int32:   32,
	Int64:   64,
	Uint8:   8,
	Uint16:  16,
	Uint32:  32,
	Uint64:  64,
	Float32: 32,
	Float64: 64,
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

// IsFloat indicates whether this subtype represents a floating-point number.
func (typ PrimitiveSubtype) IsFloat() bool {
	return typ == Float32 || typ == Float64
}

// NumberOfBits returns the number of bits used to represent this primitive in FIDL.
func (typ PrimitiveSubtype) NumberOfBits() int {
	return numberOfBits[typ]
}

// NumberOfBytes returns the number of bytes used to represent this primitive in FIDL.
func (typ PrimitiveSubtype) NumberOfBytes() int {
	return numberOfBits[typ] / 8
}

type InternalSubtype string

const (
	TransportErr InternalSubtype = "transport_error"
)

type HandleSubtype string

const (
	HandleSubtypeNone         HandleSubtype = "handle"
	HandleSubtypeBti          HandleSubtype = "bti"
	HandleSubtypeChannel      HandleSubtype = "channel"
	HandleSubtypeClock        HandleSubtype = "clock"
	HandleSubtypeDebugLog     HandleSubtype = "debuglog"
	HandleSubtypeEvent        HandleSubtype = "event"
	HandleSubtypeEventpair    HandleSubtype = "eventpair"
	HandleSubtypeException    HandleSubtype = "exception"
	HandleSubtypeFifo         HandleSubtype = "fifo"
	HandleSubtypeGuest        HandleSubtype = "guest"
	HandleSubtypeInterrupt    HandleSubtype = "interrupt"
	HandleSubtypeIommu        HandleSubtype = "iommu"
	HandleSubtypeJob          HandleSubtype = "job"
	HandleSubtypeMsi          HandleSubtype = "msi"
	HandleSubtypePager        HandleSubtype = "pager"
	HandleSubtypePciDevice    HandleSubtype = "pcidevice"
	HandleSubtypePmt          HandleSubtype = "pmt"
	HandleSubtypePort         HandleSubtype = "port"
	HandleSubtypeProcess      HandleSubtype = "process"
	HandleSubtypeProfile      HandleSubtype = "profile"
	HandleSubtypeResource     HandleSubtype = "resource"
	HandleSubtypeSocket       HandleSubtype = "socket"
	HandleSubtypeStream       HandleSubtype = "stream"
	HandleSubtypeSuspendToken HandleSubtype = "suspendtoken"
	HandleSubtypeThread       HandleSubtype = "thread"
	HandleSubtypeTime         HandleSubtype = "timer"
	HandleSubtypeVcpu         HandleSubtype = "vcpu"
	HandleSubtypeVmar         HandleSubtype = "vmar"
	HandleSubtypeVmo          HandleSubtype = "vmo"
)

// TODO(fxb/64629): Remove, source of truth is library zx.
//
// One complication is that GIDL parses nice handle subtypes in its grammar,
// e.g. `#0 = event(rights: execute + write )`. And some GIDL backends care
// about the object type. This means that we need to duplicate this mapping :/
// It would be cleaner to limit this to GIDL and GIDL backends, rather than
// offer that in the general purpose lib/declDepNode
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
	ObjectTypeDebugLog
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
	ObjectTypeStream
	ObjectTypeMsi
)

func ObjectTypeFromHandleSubtype(val HandleSubtype) ObjectType {
	switch val {
	case HandleSubtypeBti:
		return ObjectTypeBti
	case HandleSubtypeChannel:
		return ObjectTypeChannel
	case HandleSubtypeClock:
		return ObjectTypeClock
	case HandleSubtypeDebugLog:
		return ObjectTypeDebugLog
	case HandleSubtypeEvent:
		return ObjectTypeEvent
	case HandleSubtypeEventpair:
		return ObjectTypeEventPair
	case HandleSubtypeException:
		return ObjectTypeException
	case HandleSubtypeFifo:
		return ObjectTypeFifo
	case HandleSubtypeGuest:
		return ObjectTypeGuest
	case HandleSubtypeInterrupt:
		return ObjectTypeInterrupt
	case HandleSubtypeIommu:
		return ObjectTypeIommu
	case HandleSubtypeJob:
		return ObjectTypeJob
	case HandleSubtypeMsi:
		return ObjectTypeMsi
	case HandleSubtypePager:
		return ObjectTypePager
	case HandleSubtypePciDevice:
		return ObjectTypePciDevice
	case HandleSubtypePmt:
		return ObjectTypePmt
	case HandleSubtypePort:
		return ObjectTypePort
	case HandleSubtypeProcess:
		return ObjectTypeProcess
	case HandleSubtypeProfile:
		return ObjectTypeProfile
	case HandleSubtypeResource:
		return ObjectTypeResource
	case HandleSubtypeSocket:
		return ObjectTypeSocket
	case HandleSubtypeStream:
		return ObjectTypeStream
	case HandleSubtypeSuspendToken:
		return ObjectTypeSuspendToken
	case HandleSubtypeThread:
		return ObjectTypeThread
	case HandleSubtypeTime:
		return ObjectTypeTimer
	case HandleSubtypeVcpu:
		return ObjectTypeVcpu
	case HandleSubtypeVmar:
		return ObjectTypeVmar
	case HandleSubtypeVmo:
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
	BoolLiteral    LiteralKind = "bool"
	DefaultLiteral LiteralKind = "default"
)

type Literal struct {
	Kind       LiteralKind `json:"kind"`
	Value      string      `json:"value"`
	Expression string      `json:"expression"`
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
	Literal    *Literal                  `json:"literal,omitempty"`
	Value      string                    `json:"value"`
	Expression string                    `json:"expression"`
}

// Location gives the location of the FIDL declaration in its source `.fidl`
// file.
type Location struct {
	Filename string `json:"filename"`
	Line     int    `json:"line"`
	Column   int    `json:"column"`
	Length   int    `json:"length"`
}

// Compares two fidlgen.Locations lexicographically on filename and then on
// the location within the file.
func LocationCmp(a, b Location) bool {
	if cmp := strings.Compare(a.Filename, b.Filename); cmp != 0 {
		return cmp < 0
	}
	if a.Line != b.Line {
		return a.Line < b.Line
	}
	if a.Column != b.Column {
		return a.Column < b.Column
	}
	return a.Length < b.Length
}

type TypeKind string

const (
	ArrayType                 TypeKind = "array"
	VectorType                TypeKind = "vector"
	StringType                TypeKind = "string"
	HandleType                TypeKind = "handle"
	RequestType               TypeKind = "request"
	PrimitiveType             TypeKind = "primitive"
	IdentifierType            TypeKind = "identifier"
	InternalType              TypeKind = "internal"
	ZxExperimentalPointerType TypeKind = "experimental_pointer"
)

type Type struct {
	Kind               TypeKind
	ElementType        *Type
	ElementCount       *int
	HandleSubtype      HandleSubtype
	HandleRights       HandleRights
	RequestSubtype     EncodedCompoundIdentifier
	PrimitiveSubtype   PrimitiveSubtype
	Identifier         EncodedCompoundIdentifier
	InternalSubtype    InternalSubtype
	Nullable           bool
	ProtocolTransport  string
	ObjType            uint32
	ResourceIdentifier string
	TypeShapeV1        TypeShape
	TypeShapeV2        TypeShape
	PointeeType        *Type
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
	err = json.Unmarshal(*obj["type_shape_v1"], &t.TypeShapeV1)
	if err != nil {
		return err
	}
	err = json.Unmarshal(*obj["type_shape_v2"], &t.TypeShapeV2)
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
		err = json.Unmarshal(*obj["resource_identifier"], &t.ResourceIdentifier)
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
		err = json.Unmarshal(*obj["protocol_transport"], &t.ProtocolTransport)
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
		if protocolTransport, ok := obj["protocol_transport"]; ok {
			err = json.Unmarshal(*protocolTransport, &t.ProtocolTransport)
			if err != nil {
				return err
			}
		}
	case InternalType:
		err = json.Unmarshal(*obj["subtype"], &t.InternalSubtype)
		if err != nil {
			return err
		}
	case ZxExperimentalPointerType:
		t.PointeeType = &Type{}
		err = json.Unmarshal(*obj["pointee_type"], t.PointeeType)
		if err != nil {
			return err
		}
	default:
		return fmt.Errorf("Unknown type kind: %s", t.Kind)
	}

	return nil
}

// MarshalJSON customizes the JSON marshalling for Type.
func (t *Type) MarshalJSON() ([]byte, error) {
	obj := map[string]interface{}{
		"kind":          t.Kind,
		"type_shape_v1": t.TypeShapeV1,
		"type_shape_v2": t.TypeShapeV2,
	}
	switch t.Kind {
	case ArrayType:
		obj["element_type"] = t.ElementType
		obj["element_count"] = t.ElementCount
	case VectorType:
		obj["element_type"] = t.ElementType
		if t.ElementCount != nil {
			obj["maybe_element_count"] = t.ElementCount
		}
		obj["nullable"] = t.Nullable
	case StringType:
		if t.ElementCount != nil {
			obj["maybe_element_count"] = t.ElementCount
		}
		obj["nullable"] = t.Nullable
	case HandleType:
		obj["subtype"] = t.HandleSubtype
		obj["rights"] = t.HandleRights
		obj["nullable"] = t.Nullable
		obj["obj_type"] = t.ObjType
		obj["resource_identifier"] = t.ResourceIdentifier
	case RequestType:
		obj["subtype"] = t.RequestSubtype
		obj["nullable"] = t.Nullable
		obj["protocol_transport"] = t.ProtocolTransport
	case PrimitiveType:
		obj["subtype"] = t.PrimitiveSubtype
	case IdentifierType:
		obj["identifier"] = t.Identifier
		obj["nullable"] = t.Nullable
		obj["protocol_transport"] = t.ProtocolTransport
	case InternalType:
		obj["subtype"] = t.InternalSubtype
	case ZxExperimentalPointerType:
		obj["pointee_type"] = t.PointeeType
	default:
		return nil, fmt.Errorf("unknown type kind: %#v", t)
	}
	return json.Marshal(obj)
}

type AttributeArg struct {
	Name  Identifier `json:"name"`
	Value Constant   `json:"value"`
	Type  string     `json:"type"`
}

// ValueString returns the attribute arg's value in string form.
// TODO(fxbug.dev/81390): Attribute values may only be string literals for now.
// Make sure to fix this API once that changes to resolve the constant value
// for all constant types.
func (el AttributeArg) ValueString() string {
	return el.Value.Value
}

type Attribute struct {
	Name Identifier     `json:"name"`
	Args []AttributeArg `json:"arguments"`
}

func (el Attribute) LookupArg(name Identifier) (AttributeArg, bool) {
	for _, a := range el.Args {
		if a.Name == name {
			return a, true
		}
	}
	return AttributeArg{}, false
}

func (el Attribute) LookupArgStandalone() (AttributeArg, bool) {
	if len(el.Args) != 1 {
		return AttributeArg{}, false
	}
	return el.Args[0], true
}

func (el Attribute) HasArg(name Identifier) bool {
	_, ok := el.LookupArg(name)
	return ok
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

func (el Attributes) DocComments() []string {
	attr, ok := el.LookupAttribute("doc")
	if !ok {
		return nil
	}
	doc, ok := attr.LookupArgStandalone()
	docVal := doc.ValueString()
	if !ok || docVal == "" {
		return nil
	}
	return strings.Split(docVal[0:len(docVal)-1], "\n")
}

func (el Attributes) Transports() map[string]struct{} {
	transports := make(map[string]struct{})
	attr, ok := el.LookupAttribute("transport")
	if ok {
		raw, ok := attr.LookupArgStandalone()
		if ok && raw.ValueString() != "" {
			for _, transport := range strings.Split(raw.ValueString(), ",") {
				transports[strings.TrimSpace(transport)] = struct{}{}
			}
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
	attr, ok := el.LookupAttribute("bindings_denylist")
	if !ok {
		return false
	}
	raw, ok := attr.LookupArgStandalone()
	if ok && raw.ValueString() != "" {
		for _, language := range strings.Split(raw.ValueString(), ",") {
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
	HasEnvelope         bool `json:"has_envelope"`
	HasFlexibleEnvelope bool `json:"has_flexible_envelope"`
}

// FieldShape represents the shape of the field on the wire.
// See JSON IR schema, e.g. fidlc --json-schema
type FieldShape struct {
	Offset  int `json:"offset"`
	Padding int `json:"padding"`
}

// Element represents a general FIDL element: namely a declaration or one of
// its members.
type Element interface {
	GetAttributes() Attributes
	GetLocation() Location
}

var _ = []Element{
	(*Const)(nil),
	(*Bits)(nil),
	(*BitsMember)(nil),
	(*Enum)(nil),
	(*EnumMember)(nil),
	(*Resource)(nil),
	(*Service)(nil),
	(*ServiceMember)(nil),
	(*Protocol)(nil),
	(*Method)(nil),
	(*Struct)(nil),
	(*StructMember)(nil),
	(*Table)(nil),
	(*TableMember)(nil),
	(*Union)(nil),
	(*UnionMember)(nil),
	(*Alias)(nil),
	(*NewType)(nil),
}

// Decl represents a FIDL declaration.
type Decl interface {
	Element
	GetName() EncodedCompoundIdentifier
}

var _ = []Decl{
	(*Const)(nil),
	(*Bits)(nil),
	(*Enum)(nil),
	(*Resource)(nil),
	(*Service)(nil),
	(*Protocol)(nil),
	(*Struct)(nil),
	(*Table)(nil),
	(*Union)(nil),
	(*Alias)(nil),
	(*NewType)(nil),
}

type decl struct {
	Attributes
	Location `json:"location"`
	Name     EncodedCompoundIdentifier `json:"name"`
}

func (d decl) GetAttributes() Attributes {
	return d.Attributes
}

func (d decl) GetLocation() Location {
	return d.Location
}

func (d decl) GetName() EncodedCompoundIdentifier {
	return d.Name
}

// Member represents a member of FIDL layout declaration.
type Member interface {
	Element
	GetName() Identifier
}

var _ = []Member{
	(*BitsMember)(nil),
	(*EnumMember)(nil),
	(*ServiceMember)(nil),
	(*Method)(nil),
	(*StructMember)(nil),
	(*TableMember)(nil),
	(*UnionMember)(nil),
}

type member struct {
	Attributes
	Location `json:"location"`
	Name     Identifier `json:"name,omitempty"`
}

func (m member) GetAttributes() Attributes {
	return m.Attributes
}

func (m member) GetLocation() Location {
	return m.Location
}

func (m member) GetName() Identifier {
	return m.Name
}

// NamingContext represents the content of the `naming_context` JSON IR field,
// which enumerates inr order the names of the parent declarations of some
// declaration. Top-level declarations have a list of size 1, with their own
// name as the only member. Nested (ie, anonymous) declarations are lists of a
// size greater than 1, starting with the outer most ancestor declaration.
//
// While the `name` and the last string in a `naming_context` are usually
// identical, the `name` can be arbitrarily changed using the
// `@generated_name()` FIDL annotation, so this is not guaranteed to be the
// case.
type NamingContext []string

// IsAnonymous states whether the described NamingContext indicates anonymous
// declaration (ie, not
// explicitly named in the source FIDL).
func (nc NamingContext) IsAnonymous() bool {
	return len(nc) > 1
}

// Join builds a name string out of the disparate parts of the NamingContext.
func (nc NamingContext) Join() string {
	return strings.Join(nc, "")
}

// scopedNamingContext stores a NamingContext that also includes the library
// from which that naming context was sourced.  This is useful for comparing
// identical NamingContexts from different libraries for uniqueness.
type scopedNamingContext struct {
	lib EncodedLibraryIdentifier
	nc  NamingContext
}

// isDenied is a function that, given a list of denied scopedNamingContext
// prefixes generated by the deniedContexts() function, should take the supplied
// naming context and validate that none of the prefix lists in deniedContexts
// are identical to the start of this naming context. If any of them are, the
// naming context in question is attached to type or method that was declared
// anonymously inside of another declaration bearing a @bindings_denylist
// enumerating the language currently being compiled, meaning that that type or
// method should also be omitted from the output.
func (snc scopedNamingContext) isDenied(dcs []scopedNamingContext) bool {
	// For each denied naming context prefix, check to see if the candidate
	// layout's naming context starts with all of the same members and is sourced
	// from the same library. If that is the case, the naming context in question
	// is denied.
outer:
	for _, dc := range dcs {
		if snc.lib != dc.lib || len(snc.nc) < len(dc.nc) {
			continue
		}
		for i, v := range dc.nc {
			if snc.nc[i] != v {
				continue outer
			}
		}
		return true
	}
	return false
}

// LayoutDecl represents data specific to bits/enums/structs/tables/unions. All
// layouts are decls, but not all decls are layouts (e.g. protocols).
type LayoutDecl interface {
	Decl
	GetNamingContext() NamingContext
}

var _ = []LayoutDecl{
	(*Union)(nil),
	(*Table)(nil),
	(*Struct)(nil),
	(*Enum)(nil),
}

type layoutDecl struct {
	decl
	NamingContext NamingContext `json:"naming_context"`
}

func (l layoutDecl) GetNamingContext() NamingContext {
	return l.NamingContext
}

// IsAnonymous states whether this layoutDecl has an anonymous naming context. We
// treat inner layouts (i.e. layouts defined within another layout) as
// anonymous. All such layouts have a naming context with length greater than
// one, since they include at least the top level name followed by one or more
// inner names.
func (l *layoutDecl) IsAnonymous() bool {
	return l.NamingContext.IsAnonymous()
}

// A ResourceableLayoutDecl represents a layout that possesses
// "resourceness" (i.e., the ability to contain a resource type),
type ResourceableLayoutDecl interface {
	LayoutDecl
	GetResourceness() Resourceness
}

var _ = []ResourceableLayoutDecl{
	(*Union)(nil),
	(*Table)(nil),
	(*Struct)(nil),
}

type resourceableLayoutDecl struct {
	layoutDecl
	Resourceness `json:"resource"`
}

func (rl resourceableLayoutDecl) GetResourceness() Resourceness {
	return rl.Resourceness
}

// Alias represents the declaration of a FIDL alias.
type Alias struct {
	decl
	PartialTypeConstructor PartialTypeConstructor `json:"partial_type_ctor"`
}

// PartialTypeConstructor represents a FIDL type as it is constructed from
// other type arguments.
type PartialTypeConstructor struct {
	Name      EncodedCompoundIdentifier `json:"name"`
	Args      []PartialTypeConstructor  `json:"args"`
	Nullable  bool                      `json:"nullable"`
	MaybeSize *Constant                 `json:"maybe_size,omitempty"`
}

// NewType represents the declaration of a FIDL 'new type'.
type NewType struct {
	decl
	Type  Type                    `json:"type"`
	Alias *PartialTypeConstructor `json:"experimental_maybe_from_alias,omitempty"`
}

// Union represents the declaration of a FIDL union.
type Union struct {
	resourceableLayoutDecl
	Members     []UnionMember `json:"members"`
	Strictness  `json:"strict"`
	TypeShapeV1 TypeShape `json:"type_shape_v1"`
	TypeShapeV2 TypeShape `json:"type_shape_v2"`
}

// UnionMember represents the declaration of a field in a FIDL extensible
// union.
type UnionMember struct {
	member
	Reserved     bool                    `json:"reserved"`
	Ordinal      int                     `json:"ordinal"`
	Type         *Type                   `json:"type,omitempty"`
	Offset       int                     `json:"offset"`
	MaxOutOfLine int                     `json:"max_out_of_line"`
	MaybeAlias   *PartialTypeConstructor `json:"experimental_maybe_from_alias,omitempty"`
}

// Table represents a declaration of a FIDL table.
type Table struct {
	resourceableLayoutDecl
	Members     []TableMember `json:"members"`
	Strictness  `json:"strict"`
	TypeShapeV1 TypeShape `json:"type_shape_v1"`
	TypeShapeV2 TypeShape `json:"type_shape_v2"`
}

// TableMember represents the declaration of a field in a FIDL table.
type TableMember struct {
	member
	Reserved          bool                    `json:"reserved"`
	Type              *Type                   `json:"type,omitempty"`
	Ordinal           int                     `json:"ordinal"`
	MaybeDefaultValue *Constant               `json:"maybe_default_value,omitempty"`
	MaybeAlias        *PartialTypeConstructor `json:"experimental_maybe_from_alias,omitempty"`
	MaxOutOfLine      int                     `json:"max_out_of_line"`
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
	resourceableLayoutDecl
	Members     []StructMember `json:"members"`
	MaxHandles  int            `json:"max_handles"`
	TypeShapeV1 TypeShape      `json:"type_shape_v1"`
	TypeShapeV2 TypeShape      `json:"type_shape_v2"`
}

// StructMember represents the declaration of a field in a FIDL struct.
type StructMember struct {
	member
	Type              Type                    `json:"type"`
	MaybeDefaultValue *Constant               `json:"maybe_default_value,omitempty"`
	MaybeAlias        *PartialTypeConstructor `json:"experimental_maybe_from_alias,omitempty"`
	FieldShapeV1      FieldShape              `json:"field_shape_v1"`
	FieldShapeV2      FieldShape              `json:"field_shape_v2"`
}

// EmptyStructMember returns a StructMember that's suitable as the sole member
// of an empty struct.
func EmptyStructMember(name string) StructMember {
	// Empty structs have a size of 1, so the uint8 struct member returned by this
	// function can be used to pad the struct to the correct size.

	return StructMember{
		member: member{
			Name: Identifier(name),
		},
		Type: Type{
			Kind:             PrimitiveType,
			PrimitiveSubtype: Uint8,
		},
		MaybeDefaultValue: &Constant{
			Kind:       "literal",
			Identifier: "",
			Literal: &Literal{
				Kind:  "numeric",
				Value: "0",
			},
		},
	}
}

// Openness of a protocol. Affects whether unknown interaction handlers are generated. Also controls
// whether methods are allowed to be flexible, but that is enforced by fidlc, not fidlgen.
type Openness string

const (
	Open   Openness = "open"
	Ajar   Openness = "ajar"
	Closed Openness = "closed"
)

func (o Openness) IsClosed() bool {
	switch o {
	case Open, Ajar:
		return false
	case Closed, "":
		return true
	default:
		panic(fmt.Errorf("invalid openness %s", o))
	}
}

// Protocol represents the declaration of a FIDL protocol.
type Protocol struct {
	decl
	// Whether the protocol is open. This affects whether the server-side generates handlers for
	// unknown interactions.
	Openness Openness `json:"openness,omitempty"`
	// List of methods that are part of this protocol.
	Methods []Method `json:"methods"`
	// List of composed protocols.
	Composed []decl `json:"composed_protocols"`
}

// If the protocol is discoverable, gets the discovery name for the protocol, consisting of the
// library name and protocol declaration name separated by dots and enclosed in quotes. For example,
// "\"my.library.MyProtocol\"". This part of legacy service discovery (pre-RFC-0041).
func (d *Protocol) GetProtocolName() string {
	attr, ok := d.LookupAttribute("discoverable")
	if !ok {
		return ""
	}
	var name string
	if arg, ok := attr.LookupArgStandalone(); ok {
		name = arg.ValueString()
	} else {
		// TODO(fxbug.dev/102803): Construct this string in fidlc, not here.
		ci := d.Name.Parse()
		var parts []string
		for _, i := range ci.Library {
			parts = append(parts, string(i))
		}
		parts = append(parts, string(ci.Name))
		name = strings.Join(parts, ".")
	}
	return strconv.Quote(name)
}

// Returns true if this protocol must handle one-way unknown interactions.
func (p *Protocol) OneWayUnknownInteractions() bool {
	return p.Openness == Open || p.Openness == Ajar
}

// Returns true if this protocol must handle two-way unknown interactions.
func (p *Protocol) TwoWayUnknownInteractions() bool {
	return p.Openness == Open
}

// Service represents the declaration of a FIDL service.
type Service struct {
	decl
	Members []ServiceMember `json:"members"`
}

func (s *Service) GetServiceName() string {
	ci := s.Name.Parse()
	var parts []string
	for _, i := range ci.Library {
		parts = append(parts, string(i))
	}
	parts = append(parts, string(ci.Name))
	return strings.Join(parts, ".")
}

// ServiceMember represents the declaration of a field in a FIDL service.
type ServiceMember struct {
	member
	Type Type `json:"type"`
}

// Method represents the declaration of a FIDL method.
type Method struct {
	member

	// Computed ordinal to use to identify the method on the wire.
	Ordinal uint64 `json:"ordinal"`
	// Whether the method is marked as strict (other wise flexible).
	//
	// While unknown interactions are experimental, a not-set strictness should
	// be treated as strict. After unknown interactions are released this field
	// will be made required. For now, use IsStrict() to access the value
	// correctly.
	//
	// TODO(fxbug.dev/88366): make "strict" required once fidlc always provides
	// it.  It's currently gated by unknown_interactions and should not default
	// to false when not provided.
	MaybeStrict *bool `json:"strict,omitempty"`
	// True if the method was composed into this protocol from another protocol
	// definition.
	IsComposed bool `json:"is_composed"`
	// True if this method has a request. This is true for all client-initiated
	// methods, and false for server-initiated events. There may still be no
	// request payload, for example "Foo()" has a request but no request
	// payload.
	HasRequest bool `json:"has_request"`
	// The request payload of the method, given in the method arguments.
	RequestPayload *Type `json:"maybe_request_payload,omitempty"`
	// True if this method has a response. This is true for two-way methods and
	// for server-initiated events. There may still be no response payload, for
	// example "Foo(...) -> ()" or "-> Bar()" have a response but no response
	// payload.
	HasResponse bool `json:"has_response"`
	// The full response payload, as it appears on the wire. If flexible or
	// error syntax are used, this is a struct with a single field containing
	// the ResultType. Otherwise, it is the same as the return value of the
	// method.
	ResponsePayload *Type `json:"maybe_response_payload,omitempty"`
	// Whether the method uses the "error" syntax. If true, ResponsePayload will
	// be a single-element struct wrapping the result union, and response will
	// be further broken down in the ResultType, ValueType, and ErrorType
	// fields.
	HasError bool `json:"has_error"`
	// If flexible or error syntax are used, this is the type of the result
	// union containing the ValueType and (if error syntax) ErrorType.
	ResultType *Type `json:"maybe_response_result_type,omitempty"`
	// If flexible or error syntax are used, this is the type of the success
	// variant of the ResultType union, which is the same as the return value.
	ValueType *Type `json:"maybe_response_success_type,omitempty"`
	// If error syntax is used, this is the type of the error variant of the
	// ResultType union.
	ErrorType *Type `json:"maybe_response_err_type,omitempty"`
}

// GetRequestPayloadIdentifier retrieves the identifier that points to the
// declaration of the request payload.
func (m *Method) GetRequestPayloadIdentifier() (EncodedCompoundIdentifier, bool) {
	if m.RequestPayload == nil {
		return "", false
	}
	return m.RequestPayload.Identifier, true
}

// GetResponsePayloadIdentifier retrieves the identifier that points to the
// declaration of the response payload.
func (m *Method) GetResponsePayloadIdentifier() (EncodedCompoundIdentifier, bool) {
	if m.ResponsePayload == nil {
		return "", false
	}
	return m.ResponsePayload.Identifier, true
}

// Helper for getting whether the method is strict. Strict is optional while
// unknown interactions are experimental, and if strict is missing it should be
// treated as true.
// TODO(fxbug.dev/88366): replace this method with direct access to the Strict
// field, once it is required.
func (m *Method) IsStrict() bool {
	return m.MaybeStrict == nil || *m.MaybeStrict
}

// IsFlexible is the inverse of IsStrict. Convenience for templates so they
// don't have to use (not .IsStrict)
func (m *Method) IsFlexible() bool {
	return !m.IsStrict()
}

// IsTransitional returns whether this method has the `Transitional` attribute.
func (m *Method) IsTransitional() bool {
	return m.HasAttribute("transitional")
}

func (m *Method) HasRequestPayload() bool {
	return m.RequestPayload != nil
}

func (m *Method) HasResponsePayload() bool {
	return m.ResponsePayload != nil
}

// HasTransportError returns true if the method uses a result union with
// transport_err variant. This is true if it is a flexible two-way method.
func (m *Method) HasTransportError() bool {
	return m.HasRequest && m.HasResponse && m.IsFlexible()
}

// Enum represents a FIDL declaration of an enum.
type Enum struct {
	layoutDecl
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
	member
	Value Constant `json:"value"`
}

// IsUnknown indicates whether this member represents a custom unknown flexible
// enum member.
func (member *EnumMember) IsUnknown() bool {
	return member.HasAttribute("Unknown")
}

// Bits represents a FIDL declaration of an bits.
type Bits struct {
	layoutDecl
	Type       Type         `json:"type"`
	Mask       string       `json:"mask"`
	Members    []BitsMember `json:"members"`
	Strictness `json:"strict"`
}

// BitsMember represents a single variant in a FIDL bits.
type BitsMember struct {
	member
	Value Constant `json:"value"`
}

// Const represents a FIDL declaration of a named constant.
type Const struct {
	decl
	Type  Type     `json:"type"`
	Value Constant `json:"value"`
}

// Resource gives the declaration of a FIDL resource.
type Resource struct {
	decl
	Type       Type               `json:"type"`
	Properties []ResourceProperty `json:"properties"`
}

type ResourceProperty struct {
	decl
	Type Type `json:"type"`
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

// Note: Keep `DeclType` values in sync with `GetDeclType` below!
const (
	LibraryDeclType DeclType = "library"

	AliasDeclType    DeclType = "alias"
	BitsDeclType     DeclType = "bits"
	ConstDeclType    DeclType = "const"
	EnumDeclType     DeclType = "enum"
	NewTypeDeclType  DeclType = "new_type"
	ProtocolDeclType DeclType = "protocol"
	ResourceDeclType DeclType = "resource"
	ServiceDeclType  DeclType = "service"
	StructDeclType   DeclType = "struct"
	TableDeclType    DeclType = "table"
	UnionDeclType    DeclType = "union"
)

func GetDeclType(decl Decl) DeclType {
	switch decl.(type) {
	case *Const:
		return ConstDeclType
	case *Bits:
		return BitsDeclType
	case *Enum:
		return EnumDeclType
	case *Resource:
		return ResourceDeclType
	case *Protocol:
		return ProtocolDeclType
	case *Service:
		return ServiceDeclType
	case *Struct:
		return StructDeclType
	case *Table:
		return TableDeclType
	case *Union:
		return UnionDeclType
	case *Alias:
		return AliasDeclType
	case *NewType:
		return NewTypeDeclType
	}
	panic(fmt.Sprintf("unhandled declaration type: %s", reflect.TypeOf(decl).Name()))
}

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
	Name  EncodedLibraryIdentifier `json:"name"`
	Decls DeclInfoMap              `json:"declarations"`
}

// Root is the top-level object for a FIDL library.
// It contains lists of all declarations and dependencies within the library.
type Root struct {
	Name            EncodedLibraryIdentifier    `json:"name"`
	Experiments     Experiments                 `json:"experiments,omitempty"`
	Consts          []Const                     `json:"const_declarations"`
	Bits            []Bits                      `json:"bits_declarations"`
	Enums           []Enum                      `json:"enum_declarations"`
	Resources       []Resource                  `json:"experimental_resource_declarations"`
	Protocols       []Protocol                  `json:"protocol_declarations"`
	Services        []Service                   `json:"service_declarations"`
	Structs         []Struct                    `json:"struct_declarations"`
	ExternalStructs []Struct                    `json:"external_struct_declarations"`
	Tables          []Table                     `json:"table_declarations"`
	Unions          []Union                     `json:"union_declarations"`
	Aliases         []Alias                     `json:"alias_declarations"`
	NewTypes        []NewType                   `json:"new_type_declarations"`
	DeclOrder       []EncodedCompoundIdentifier `json:"declaration_order"`
	Decls           DeclMap                     `json:"declarations"`
	Libraries       []Library                   `json:"library_dependencies"`
}

// ForEachDecl calls a provided callback on each associated declaration. Logic
// that needs to iterate over all declarations should rely on this method as
// opposed to hardcoding the known (at the time) set of declaration types.
func (r *Root) ForEachDecl(cb func(Decl)) {
	for i := range r.Consts {
		cb(&r.Consts[i])
	}
	for i := range r.Bits {
		cb(&r.Bits[i])
	}
	for i := range r.Enums {
		cb(&r.Enums[i])
	}
	for i := range r.Resources {
		cb(&r.Resources[i])
	}
	for i := range r.Protocols {
		cb(&r.Protocols[i])
	}
	for i := range r.Services {
		cb(&r.Services[i])
	}
	for i := range r.Structs {
		cb(&r.Structs[i])
	}
	for i := range r.ExternalStructs {
		cb(&r.ExternalStructs[i])
	}
	for i := range r.Tables {
		cb(&r.Tables[i])
	}
	for i := range r.Unions {
		cb(&r.Unions[i])
	}
	for i := range r.Aliases {
		cb(&r.Aliases[i])
	}
	for i := range r.NewTypes {
		cb(&r.NewTypes[i])
	}
}

// DeclInfo returns information on the FIDL library's local and imported
// declarations.
func (r *Root) DeclInfo() DeclInfoMap {
	m := DeclInfoMap{}
	r.ForEachDecl(func(decl Decl) {
		info := DeclInfo{
			Type: GetDeclType(decl),
		}
		if resDecl, ok := decl.(ResourceableLayoutDecl); ok {
			info.Resourceness = new(Resourceness)
			*info.Resourceness = resDecl.GetResourceness()
		}
		m[decl.GetName()] = info
	})
	for _, l := range r.Libraries {
		for k, v := range l.Decls {
			m[k] = v
		}
	}
	return m
}

type EncodedCompoundIdentifierSet map[EncodedCompoundIdentifier]struct{}

// GetMessageBodyTypeNames calculates set of ECIs that refer to types used as
// message bodies by this library.
func (r *Root) GetMessageBodyTypeNames() EncodedCompoundIdentifierSet {
	mbtn := EncodedCompoundIdentifierSet{}
	for _, protocol := range r.Protocols {
		for _, method := range protocol.Methods {
			if method.RequestPayload != nil {
				mbtn[method.RequestPayload.Identifier] = struct{}{}
			}
			if method.ResponsePayload != nil {
				mbtn[method.ResponsePayload.Identifier] = struct{}{}
			}
		}
	}
	return mbtn
}

// payloadTypeNames calculates set of ECIs that refer to types used user-defined
// payloads by this library. Specifically, for the following FIDL method
// definition:
//
// MyMethod(struct{...}) -> (struct{...}) error uint32;
//
//	|-----A-----|    |-----B-----| |-----C-----|
//	                 |------------D------------|
//
// types `A` and `B` are payloads, but `C` (the error) and `D` (the result) are
// not. If the `error` syntax is not used, the message body type name and
// payload type name sets for a method are identical.
func (r *Root) payloadTypeNames() EncodedCompoundIdentifierSet {
	ptn := EncodedCompoundIdentifierSet{}
	for _, protocol := range r.Protocols {
		for _, method := range protocol.Methods {
			if method.RequestPayload != nil {
				ptn[method.RequestPayload.Identifier] = struct{}{}
			}
			if method.ResponsePayload != nil {
				if method.HasError || method.HasTransportError() {
					ptn[method.ValueType.Identifier] = struct{}{}
				} else {
					ptn[method.ResponsePayload.Identifier] = struct{}{}
				}
			}
		}
	}
	return ptn
}

// MethodTypeUsage is an enum that stores whether a type referenced by a payload
// is used only as a payload parameter list, a wire message shape, or both. For
// example, consider the following FIDL method:
//
//	MyMethod(struct{...}) -> (struct{...}) error uint32;
//	        |-----A-----|    |-----B-----| |-----C-----|
//	                         |------------D------------|
//
// Types `B` is `OnlyPayloadMethodTypeUsage` (it is exposed to the user, but
// never sent over the wire), type `D` is `OnlyWireMethodTypeUsage` (it
// describes the shape of a message body sent over the wire, but the wrapper
// struct is never exposed to the user as a payload that they may send) and type
// `A` is `BothMethodTypeUsage` (it is both exposed to the user, via the
// signature of the request-sending function, and describes the shape of the
// message sent over the wire).
type MethodTypeUsage string

const (
	UsedOnlyAsPayload               MethodTypeUsage = "asOnlyPayload"
	UsedOnlyAsMessageBody           MethodTypeUsage = "asOnlyWire"
	UsedBothAsPayloadAndMessageBody MethodTypeUsage = "asBoth"
)

// MethodTypeUsageMap maps the names of types referenced by methods (ResultType,
// ValueType, ResponsePayload) to the MethodTypeUsage exhibited by that type.
type MethodTypeUsageMap map[EncodedCompoundIdentifier]MethodTypeUsage

// MethodTypeUsageMap creates a map from the names of all non-error types
// references by methods to their MethodTypeUsage.
func (r *Root) MethodTypeUsageMap() MethodTypeUsageMap {
	out := MethodTypeUsageMap{}
	mbtn := r.GetMessageBodyTypeNames()
	ptn := r.payloadTypeNames()
	for name := range mbtn {
		if _, ok := ptn[name]; ok {
			out[name] = UsedBothAsPayloadAndMessageBody
			delete(ptn, name)
		} else {
			out[name] = UsedOnlyAsMessageBody
		}
	}
	for name := range ptn {
		out[name] = UsedOnlyAsPayload
	}
	return out
}

// deniedContexts produces a list of scopedNamingContexts. Any types/methods that begin with the
// scopedNamingContext in that list should be denied as well when run through the isDenied()
// function.
func deniedContexts(r *Root, language string) []scopedNamingContext {
	var denied []scopedNamingContext
	r.ForEachDecl(func(decl Decl) {
		if layout, ok := decl.(LayoutDecl); ok {
			if layout.GetAttributes().BindingsDenylistIncludes(language) {
				denied = append(denied, scopedNamingContext{layout.GetName().LibraryName(), layout.GetNamingContext()})
			}
		}

		if protocol, ok := decl.(*Protocol); ok {
			protocolName := string(protocol.Name.Parse().Name)
			if protocol.BindingsDenylistIncludes(language) {
				denied = append(denied, scopedNamingContext{protocol.Name.LibraryName(), []string{protocolName}})
			} else {
				for _, m := range protocol.Methods {
					if m.BindingsDenylistIncludes(language) {
						denied = append(denied, scopedNamingContext{
							protocol.Name.LibraryName(),
							[]string{protocolName, string(m.Name)},
						})
					}
				}
			}
		}
	})

	return denied
}

// ForBindings filters out declarations that should be omitted in the given
// language bindings based on BindingsDenylist attributes. It returns a new Root
// and does not modify r.
func (r *Root) ForBindings(language string) Root {
	denied := deniedContexts(r, language)
	res := Root{
		Name:        r.Name,
		Experiments: r.Experiments,
		Libraries:   r.Libraries,
		Decls:       make(DeclMap, len(r.Decls)),
	}

	r.ForEachDecl(func(decl Decl) {
		if decl.GetAttributes().BindingsDenylistIncludes(language) {
			return
		}
		if layout, ok := decl.(LayoutDecl); ok {
			scoped := scopedNamingContext{r.Name, layout.GetNamingContext()}
			if scoped.isDenied(denied) {
				return
			}
		}

		switch v := decl.(type) {
		case *Const:
			res.Consts = append(res.Consts, *v)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Bits:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Bits = append(res.Bits, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Enum:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Enums = append(res.Enums, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Protocol:
			newV := *v
			newV.Methods = nil
			for _, m := range v.Methods {
				nc := NamingContext{string(v.Name), string(m.Name)}
				if !m.BindingsDenylistIncludes(language) && !(scopedNamingContext{r.Name, nc}.isDenied(denied)) {
					newV.Methods = append(newV.Methods, m)
				}
			}
			res.Protocols = append(res.Protocols, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Service:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			res.Services = append(res.Services, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Struct:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				}
			}
			if v.Name.LibraryName() == r.Name {
				res.Structs = append(res.Structs, newV)
			} else {
				res.ExternalStructs = append(res.ExternalStructs, newV)
			}
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Table:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				} else {
					newV.Members = append(newV.Members, TableMember{
						member: member{
							Attributes: m.Attributes,
							Name:       m.Name,
						},
						Reserved: true,
						Ordinal:  m.Ordinal,
					})
				}
			}
			res.Tables = append(res.Tables, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Union:
			newV := *v
			newV.Members = nil
			for _, m := range v.Members {
				if !m.BindingsDenylistIncludes(language) {
					newV.Members = append(newV.Members, m)
				} else {
					newV.Members = append(newV.Members, UnionMember{
						member: member{
							Attributes: m.Attributes,
							Name:       m.Name,
						},
						Reserved: true,
						Ordinal:  m.Ordinal,
					})
				}
			}
			res.Unions = append(res.Unions, newV)
			res.Decls[v.Name] = r.Decls[v.Name]
		case *Alias:
			res.Aliases = append(res.Aliases, *v)
			res.Decls[v.Name] = r.Decls[v.Name]
		}
	})

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

func Int64OrUint64FromInt64ForTesting(val int64) int64OrUint64 {
	if val >= 0 {
		return int64OrUint64{0, uint64(val)}
	}
	return int64OrUint64{val, 0}
}

func Int64OrUint64FromUint64ForTesting(val uint64) int64OrUint64 {
	return int64OrUint64{0, val}
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

func (n *int64OrUint64) MarshalJSON() ([]byte, error) {
	if n.i != 0 {
		return json.Marshal(n.i)
	}
	return json.Marshal(n.u)
}
