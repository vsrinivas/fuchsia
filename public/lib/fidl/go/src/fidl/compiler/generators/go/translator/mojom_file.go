// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"
	"strings"
)

// TmplFile contains all of the information needed by the templates to generate
// the go bindings for one mojom file.
type TmplFile struct {
	PackageName               string
	Imports                   []Import
	Structs                   []*StructTemplate
	Unions                    []*UnionTemplate
	Enums                     []*EnumTemplate
	Interfaces                []*InterfaceTemplate
	Constants                 []*ConstantTemplate
	MojomImports              []string
	SerializedRuntimeTypeInfo string
}

type Import struct {
	PackagePath string
	PackageName string
}

func (t *TmplFile) TypesPkg() string {
	if t.PackageName != "mojom_types" {
		return "mojom_types."
	}
	return ""
}

func (t *TmplFile) DescPkg() string {
	if t.PackageName != "service_describer" {
		return "service_describer."
	}
	return ""
}

////////////////////////////////////////////////////////////////////////////////

// StructTemplate contains all of the information needed by the templates to
// generate the go bindings for one mojom struct.
type StructTemplate struct {
	// Name is the name of the structure in go code.
	Name string

	// PrivateName is identical to Name with the first letter lower cased.
	PrivateName string

	// Fields contains the list of fields of the struct.
	Fields []StructFieldTemplate

	// CurVersionNumber is the latest version number for the struct.
	CurVersionNumber uint32

	// CurVersionSize is the size in bytes of the latest version number for the struct.
	CurVersionSize uint32

	// Versions is the list of versions of the struct.
	Versions []structVersion

	// NestedEnums contains the enums nested in the struct.
	NestedEnums []*EnumTemplate

	TypeKey string
}

func (s *StructTemplate) ConcreteName() string {
	return s.Name
}

func (s *StructTemplate) InterfaceName() string {
	return s.Name
}

// StructFieldTemplate contains all the information necessary to generate
// declaration, encoding and decoding code for a field.
type StructFieldTemplate struct {
	// Type is the go type of the field.
	Type string

	// Name is the name of the field in go.
	Name string

	// IsNullable is true iff the field is nullable.
	IsNullable bool

	// MinVersion is the
	MinVersion uint32

	// EncodingInfo contains the information necessary to encode and decode the field.
	EncodingInfo EncodingInfo
}

// structVersion contains the description of a struct's version.
type structVersion struct {
	// NumBytes is the size of the struct in bytes at that version.
	NumBytes uint32

	// Version is the version number.
	Version uint32
}

////////////////////////////////////////////////////////////////////////////////

type UnionTemplate struct {
	// Name is the name of the union in go code.
	Name string

	// Fields contains the list of fields of the union.
	Fields []UnionFieldTemplate

	TypeKey string
}

type UnionFieldTemplate struct {
	// Name is the name of the field in go.
	Name string

	// Type is the go type of the field value.
	Type string

	// Tag of the field.
	Tag uint32

	// Union is the union containing this field.
	Union *UnionTemplate

	// EncodingInfo contains the information necessary to encode and decode the field.
	EncodingInfo EncodingInfo
}

func (u *UnionFieldTemplate) ConcreteName() string {
	return u.Union.Name + u.Name
}

func (u *UnionFieldTemplate) InterfaceName() string {
	return u.Union.Name
}

////////////////////////////////////////////////////////////////////////////////

type EnumTemplate struct {
	// Name is the name of the enum type in go code.
	Name string

	// Values contains the list of possible values for the enum.
	Values []EnumValueTemplate

	TypeKey string
}

func (e *EnumTemplate) ConcreteName() string {
	return e.Name
}

func (e *EnumTemplate) InterfaceName() string {
	return e.Name
}

type EnumValueTemplate struct {
	// Name of the enum value in go.
	Name string

	// Value is the numeric representation of the enum value.
	Value int32
}

////////////////////////////////////////////////////////////////////////////////

type InterfaceTemplate struct {
	// Name is the name of the interface.
	Name string

	// PrivateName is identical to Name with the first letter lower cased.
	PrivateName string

	// ServiceName is the service name of the interface.
	ServiceName *string

	// Methods contains the list of methods of the interface.
	Methods []MethodTemplate

	// NestedEnums contains the enums nested in the interface.
	NestedEnums []*EnumTemplate

	TypeKey string
}

func (i *InterfaceTemplate) ConcreteName() string {
	return i.Name + "_Request"
}

func (i *InterfaceTemplate) InterfaceName() string {
	return i.Name
}

type MethodTemplate struct {
	// MethodName is the name of the method.
	MethodName string

	// FullName is a composite of the interface name and the method name.
	FullName string

	// Ordinal of the method.
	Ordinal uint32

	// Params of the method.
	Params StructTemplate

	// ResponseParams of the method.
	ResponseParams *StructTemplate

	// Interface is the interface that contains this method.
	Interface *InterfaceTemplate
}

////////////////////////////////////////////////////////////////////////////////

type ConstantTemplate struct {
	Name  string
	Type  string
	Value string
}

////////////////////////////////////////////////////////////////////////////////

// EncodingInfo describes the information necessary to encode a field.
type EncodingInfo interface {
	// IsSimple returns true if the field is a numeric type, boolean or string.
	IsSimple() bool

	// IsHandle returns true if the field is a handle type.
	IsHandle() bool

	// IsPointer returns true if the field is a pointer: struct, array, map or string.
	IsPointer() bool

	// IsStruct returns true if the field is a struct.
	IsStruct() bool

	// IsUnion returns true if the field is a union.
	IsUnion() bool

	// IsEnum returns true if the field is an enum.
	IsEnum() bool

	// IsInterface returns true if the field is an interface or interface request.
	IsInterface() bool

	// IsInterfaceRequest returns true if the field is an interface request.
	IsInterfaceRequest() bool

	// IsArray returns true if the field is an array.
	IsArray() bool

	// IsMap returns true if the field is a map.
	IsMap() bool

	// IsNullable returns true if the field is nullable.
	IsNullable() bool
	setNullable(nullable bool)

	// FixedSize returns the fixed size of an array if there is one.
	FixedSize() int32

	// HasFixedSize returns true if the field is an array with a fixed size.
	HasFixedSize() bool

	// ElementEncodingInfo returns the EncodingInfo of the elements on an array.
	ElementEncodingInfo() EncodingInfo

	// KeyEncodingInfo returns the EncodingInfo of the keys of a map.
	KeyEncodingInfo() EncodingInfo

	// ValueEncodingInfo returns the EncodingInfo of the values of a map.
	ValueEncodingInfo() EncodingInfo

	// BitSize is the size of the field in bits.
	BitSize() uint32

	// WriteFunction is the encoder method for the field's type.
	WriteFunction() string

	// ReadFunction is the decoder method for the field's type.
	ReadFunction() string

	// GoType returns the type of the field.
	GoType() string
	setGoType(goType string)

	// BaseGoType returns the base type of the field.
	BaseGoType() string

	// Identifier returns the qualified identifier referring to the field in
	// question. If this is the encoding info of array elements, map keys or
	// values, the Identifier is generated to facilitate iteration.
	Identifier() string
	setIdentifier(id string)

	// DerefIdentifier returns the dereferenced qualified identifier to the field
	// in question.
	DerefIdentifier() string
}

////////////////////////////////////////////////////////////////////////////////

// baseEncodingInfo implements the EncodingInfo interface and makes it easy to
// implement EncodingInfo for various types.
type baseEncodingInfo struct {
	identifier string
	goType     string
}

func (b *baseEncodingInfo) Identifier() string {
	return b.identifier
}

func (b *baseEncodingInfo) DerefIdentifier() (id string) {
	return b.Identifier()
}

func (b *baseEncodingInfo) setIdentifier(identifier string) {
	b.identifier = identifier
}

func (b *baseEncodingInfo) IsSimple() bool {
	return false
}

func (b *baseEncodingInfo) IsHandle() bool {
	return false
}

func (b *baseEncodingInfo) IsPointer() bool {
	return false
}

func (b *baseEncodingInfo) IsStruct() bool {
	return false
}

func (b *baseEncodingInfo) IsUnion() bool {
	return false
}

func (b *baseEncodingInfo) IsEnum() bool {
	return false
}

func (b *baseEncodingInfo) IsInterface() bool {
	return false
}

func (b *baseEncodingInfo) IsInterfaceRequest() bool {
	return false
}

func (b *baseEncodingInfo) IsArray() bool {
	return false
}

func (b *baseEncodingInfo) IsMap() bool {
	return false
}

func (b *baseEncodingInfo) FixedSize() int32 {
	panic("Only arrays can have a fixed size.")
}

func (b *baseEncodingInfo) HasFixedSize() bool {
	return false
}

func (b *baseEncodingInfo) ElementEncodingInfo() EncodingInfo {
	panic("Only arrays have elements!")
}

func (b *baseEncodingInfo) KeyEncodingInfo() EncodingInfo {
	panic("Only maps have keys!")
}

func (b *baseEncodingInfo) ValueEncodingInfo() EncodingInfo {
	panic("Only maps have values!")
}

func (b *baseEncodingInfo) IsNullable() bool {
	return false
}

func (b *baseEncodingInfo) setNullable(nullable bool) {
	_ = nullable
	panic("setNullable not implemented for non-pointers.")
}

func (b *baseEncodingInfo) GoType() string {
	return b.goType
}

func (b *baseEncodingInfo) setGoType(goType string) {
	b.goType = goType
}

func (b *baseEncodingInfo) BaseGoType() string {
	goType := b.GoType()
	if goType[0] == '*' {
		return goType[1:]
	}
	return goType
}

////////////////////////////////////////////////////////////////////////////////

type baseNullableEncodingInfo struct {
	baseEncodingInfo
	nullable bool
}

func (b *baseNullableEncodingInfo) DerefIdentifier() (id string) {
	id = b.Identifier()
	if b.IsNullable() && !b.IsUnion() {
		id = fmt.Sprintf("(*%s)", id)
	}
	return id
}

func (b *baseNullableEncodingInfo) IsNullable() bool {
	return b.nullable
}

func (b *baseNullableEncodingInfo) setNullable(nullable bool) {
	b.nullable = nullable
}

////////////////////////////////////////////////////////////////////////////////

// basePointerEncodingInfo implements the EncodingInfo for pointer types.
type basePointerEncodingInfo struct {
	baseNullableEncodingInfo
}

func (b *basePointerEncodingInfo) IsPointer() bool {
	return true
}

func (b *basePointerEncodingInfo) BitSize() uint32 {
	return uint32(64)
}

////////////////////////////////////////////////////////////////////////////////

// simpleTypeEncodingInfo is the EncodingInfo for a numeric or boolean type.
type simpleTypeEncodingInfo struct {
	baseEncodingInfo
	bitSize       uint32
	writeFunction string
	readFunction  string
}

func (t *simpleTypeEncodingInfo) BitSize() uint32 {
	return t.bitSize
}

func (t *simpleTypeEncodingInfo) IsSimple() bool {
	return true
}

func (t *simpleTypeEncodingInfo) WriteFunction() string {
	return t.writeFunction
}

func (t *simpleTypeEncodingInfo) ReadFunction() string {
	return t.readFunction
}

////////////////////////////////////////////////////////////////////////////////

// stringTypeEncodingInfo is the EncodingInfo for a string.
type stringTypeEncodingInfo struct {
	basePointerEncodingInfo
}

func (t *stringTypeEncodingInfo) IsSimple() bool {
	return true
}

func (t *stringTypeEncodingInfo) WriteFunction() string {
	return "WriteString"
}

func (t *stringTypeEncodingInfo) ReadFunction() string {
	return "ReadString"
}

////////////////////////////////////////////////////////////////////////////////

type handleTypeEncodingInfo struct {
	baseNullableEncodingInfo
	readFunction string
}

func (t *handleTypeEncodingInfo) IsHandle() bool {
	return true
}

func (t *handleTypeEncodingInfo) BitSize() uint32 {
	return 32
}

func (t *handleTypeEncodingInfo) WriteFunction() string {
	panic("No write function needs to be specified for handles.")
}

func (t *handleTypeEncodingInfo) ReadFunction() string {
	return t.readFunction
}

////////////////////////////////////////////////////////////////////////////////

// arrayTypeEncodingInfo is the EncodingInfo for an array.
type arrayTypeEncodingInfo struct {
	basePointerEncodingInfo
	elementEncodingInfo EncodingInfo
	fixedSize           int32
}

func (t *arrayTypeEncodingInfo) IsArray() bool {
	return true
}

func (t *arrayTypeEncodingInfo) HasFixedSize() bool {
	return (t.fixedSize >= 0)
}

func (t *arrayTypeEncodingInfo) FixedSize() int32 {
	if t.fixedSize < 0 {
		panic("Array does not have fixed size.")
	}
	return t.fixedSize
}

func (t *arrayTypeEncodingInfo) ElementEncodingInfo() EncodingInfo {
	return t.elementEncodingInfo
}

func (t *arrayTypeEncodingInfo) WriteFunction() string {
	panic("Arrays don't have a write function.")
}

func (t *arrayTypeEncodingInfo) ReadFunction() string {
	panic("Arrays don't have a read function.")
}

////////////////////////////////////////////////////////////////////////////////

// mapTypeEncodingInfo is the EncodingInfo for a map.
type mapTypeEncodingInfo struct {
	basePointerEncodingInfo
	keyEncodingInfo   EncodingInfo
	valueEncodingInfo EncodingInfo
}

func (t *mapTypeEncodingInfo) IsMap() bool {
	return true
}

func (t *mapTypeEncodingInfo) KeyEncodingInfo() EncodingInfo {
	return t.keyEncodingInfo
}

func (t *mapTypeEncodingInfo) ValueEncodingInfo() EncodingInfo {
	return t.valueEncodingInfo
}

func (t *mapTypeEncodingInfo) WriteFunction() string {
	panic("Maps don't have a write function.")
}

func (t *mapTypeEncodingInfo) ReadFunction() string {
	panic("Maps don't have a read function.")
}

////////////////////////////////////////////////////////////////////////////////

// structTypeEncodingInfo is the EncodingInfo for a struct.
type structTypeEncodingInfo struct {
	basePointerEncodingInfo
}

func (t *structTypeEncodingInfo) IsStruct() bool {
	return true
}

func (t *structTypeEncodingInfo) WriteFunction() string {
	panic("Structs don't have a write function.")
}

func (t *structTypeEncodingInfo) ReadFunction() string {
	panic("Structs don't have a read function.")
}

////////////////////////////////////////////////////////////////////////////////

// unionTypeEncodingInfo is the EncodingInfo for a union.
type unionTypeEncodingInfo struct {
	baseNullableEncodingInfo
	nestedUnion bool
}

func (t *unionTypeEncodingInfo) BitSize() uint32 {
	if t.nestedUnion {
		return uint32(64)
	}
	return uint32(128)
}

func (t *unionTypeEncodingInfo) IsUnion() bool {
	return true
}

func (t *unionTypeEncodingInfo) IsPointer() bool {
	return t.nestedUnion
}

func (t *unionTypeEncodingInfo) WriteFunction() string {
	panic("Unions don't have a write function.")
}

func (t *unionTypeEncodingInfo) ReadFunction() string {
	panic("Unions don't have a read function.")
}

func (t *unionTypeEncodingInfo) UnionDecodeFunction() string {
	goTypeSplit := strings.Split(t.BaseGoType(), ".")
	if len(goTypeSplit) == 1 {
		return fmt.Sprintf("Decode%s", goTypeSplit[0])
	}
	return fmt.Sprintf("%s.Decode%s", goTypeSplit[0], goTypeSplit[1])
}

////////////////////////////////////////////////////////////////////////////////

type enumTypeEncodingInfo struct {
	baseEncodingInfo
}

func (t *enumTypeEncodingInfo) BitSize() uint32 {
	// enums are encoded as 32 bit unsigned integers.
	return uint32(32)
}

func (t *enumTypeEncodingInfo) IsEnum() bool {
	return true
}

func (t *enumTypeEncodingInfo) WriteFunction() string {
	panic("Enums don't have a write function.")
}

func (t *enumTypeEncodingInfo) ReadFunction() string {
	return "ReadInt32"
}

////////////////////////////////////////////////////////////////////////////////

type interfaceTypeEncodingInfo struct {
	baseNullableEncodingInfo
	interfaceRequest bool
}

func (t *interfaceTypeEncodingInfo) IsInterface() bool {
	return true
}

func (t *interfaceTypeEncodingInfo) IsInterfaceRequest() bool {
	return t.interfaceRequest
}

func (t *interfaceTypeEncodingInfo) BitSize() uint32 {
	if t.interfaceRequest {
		return uint32(32)
	} else {
		return uint32(64)
	}
}

func (t *interfaceTypeEncodingInfo) WriteFunction() string {
	if t.interfaceRequest {
		return "WriteHandle"
	} else {
		return "WriteInterface"
	}
}

func (t *interfaceTypeEncodingInfo) ReadFunction() string {
	if t.interfaceRequest {
		return "ReadMessagePipeHandle"
	} else {
		return "ReadInterface"
	}
}
