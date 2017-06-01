// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fidl/compiler/lexer"
	"fmt"
	"math"
)

/*
This file contains data structures and functions used to describe Mojom types,
values, type references and value references. The difference between a type and
a type reference is indicated by the fact that, for example, there is only
one int32 type but a .mojom file may contain many references to that type. For
the built-in types like int32 this distinction is not important and the same
object will represent both the type and the type reference. But for a
user-defined type the distinction is important. The type "struct Foo" is
created via a mojom struct declaration and then the type is referenced in
other places via an identifier. We will use different objects to represent
the type and the (possibly mulitple) references to the type.

This file does not contain the objects that represent user-defined types.
Those may be found in the file user_defined_types.go. This file does
contain the objects that represent built-in types and type references and
user-defined type references. Type resolution refers to the process of
mapping each user-defined type reference to its corresponding user-defined
type.

The situation is similar with values. This file contains the objects that
represent built-in values and value references and user-defined value
references. The objects that represent user-defined values may also
be found in the file user_defined_types.go. Value resolution refers to the
process of mapping each user-defined value reference to its corresponding
user-defined value.
*/

// TypeKind represents the different kinds of Mojom types (and type references).
type TypeKind int

const (
	TypeKindSimple TypeKind = iota
	TypeKindString
	TypeKindArray
	TypeKindMap
	TypeKindHandle
	TypeKindUserDefined
)

// LiteralType represents the type of a literal. The literal types
// are the simple types plus string. This interface is used to represent
// literal types in their aspect as a type as opposed to a type reference.
// A LiteralType is a |ConcreteType|.
type LiteralType interface {
	ConcreteType
	LiteralTypeKind() TypeKind
}

// ConcreteType represents the type of a concrete value. These are the types
// that may be the type of a constant declaration, a default value assignment,
// or in some cases, an enum value initializer. The concrete types are
// the LiteralTypes, the BuiltInValuesType and the enum types.
// This interface represents types as opposed to type references.
type ConcreteType interface {
	String() string
	ConcreteTypeKind() TypeKind
}

// TypeRef represents a reference to any kind of type. An instance of TypeRef
// represents a particular textual occurrence of a type reference.
type TypeRef interface {
	String() string
	TypeRefKind() TypeKind

	// MarkUsedAsMapKey records the fact that the type being referenced is
	// being used as the type of a map key. Returns false if we already have
	// enough information to know that this is not allowed. This method is
	// used during parsing.
	MarkUsedAsMapKey() (ok bool)

	// MarkUsedAsConstantType records the fact that the type being referenced is
	// being used as the type of declared constant. Returns false if we already
	// have enough information to know that this is not allowed. This method
	// is used during parsing.
	MarkUsedAsConstantType() (ok bool)

	// MarkTypeCompatible records the fact that the type being referenced is
	// being used as the type of a variable being assigned a literal value
	// described by |LiteralAssignment|. Returns false if we already have enough
	// information to know that this is not allowed. This method is used
	// during parsing.
	MarkTypeCompatible(assignment LiteralAssignment) (ok bool)

	// IsAssignmentCompatible() determines whether the given ConcreteValue may
	// be assigned to a variable of this type. This method is similar to
	// MarkTypeCompatible() but it is used after resolution during the
	// validation phase whereas MarkTypeCompatible() is used during parsing.
	// More specifically there are the following differences:
	// -- This method accepts EnumValues and BuiltInConstantValues in addition
	// to LiteralValues.
	// -- For UserValueRefs this method should only be invoked after resolution
	// because it consults the resolved type to determine compatibility instead
	// of marking the reference for later checking.
	// -- This method must never be invoked on HandleTypeRefs, ArrayTypeRefs,
	// or MapTypeRefs because those types are never validated after resolution.
	IsAssignmentCompatible(assignedValue ConcreteValue) bool

	// TypeName() returns a string appropriate for user-facing messages
	// that names a type, including markers for interface requests and nullability.
	// For UserDefinedTypes it will return the fully-qualified
	// name of the resolved type, if the identifier has already been resolved.
	// Otherwise it will return the identifier.
	TypeName() string

	// Returns true just in case this type reference is allowed as the type of
	// a struct field in a non-zero version of a struct.  We disallow
	// pointer and handle types that are not nullable. This means we allow all primitive
	// types and enums, and nullable strings, arrays, maps, handles, interfaces
	// structs and unions. This method should only be invoked after successful
	// parsing and resolution have occurred.
	AllowedInNonZeroStructVersion() bool

	// SerializationSize() is invoked after the resolution and validation phases.
	// It returns the number of bytes necessary to serialize an instance of the type
	// to which this type reference resolves in Mojo serialization.
	SerializationSize() uint32

	// SerializationAlignment() is invoked after the resolution and validation phases.
	// It returns the number of bytes on which an instance of the type to which this
	// type reference resolves must be aligned during Mojo serialization.
	SerializationAlignment() uint32

	// NonAvoidableUserType is invoked after the resolution and validation phases.
	// It returns a non-nil UserDefinedType just in case this type
	// reference resolves to a type the serialization of which will
	// include a non-nullable pointer to a struct or union. In that case a
	// non-nil UserDefinedType representing that struct or union is returnd.
	//
	// This method is used during the analysis of ill-founded types. The goal of
	// that analysis is to find user-defined types that are unserializable because
	// every instance of the type would involve a cycle.
	//
	// For example, suppose there is a struct Foo that contains a field |x|
	// and the type of x is Foo. An instance of Foo could not be serialized because
	// the cycle would cause an infinite recursion.
	//
	// However a cycle in the type graph is allowed if it is avoidable.
	// For example if the type of x were instead Foo? then an instance
	// of Foo could be serialized by setting x to null at some level
	// of the recursion.
	//
	// Similarly if the type of x were array<Foo, 1> then an instance of
	// Foo would not be serializable. However it allowed for the type of
	// x to be any of the following:
	//   - array<Foo>
	//   - array<Foo?, 1>
	//   - array<Foo, 1>?
	NonAvoidableUserType() UserDefinedType
}

/////////////////////////////////////////////////////////////
// SimpleType
/////////////////////////////////////////////////////////////
type SimpleType int

const (
	SimpleTypeBool SimpleType = iota
	SimpleTypeDouble
	SimpleTypeFloat
	SimpleTypeInt8
	SimpleTypeInt16
	SimpleTypeInt32
	SimpleTypeInt64
	SimpleTypeUInt8
	SimpleTypeUInt16
	SimpleTypeUInt32
	SimpleTypeUInt64
)

var allSimpleTypes = []SimpleType{SimpleTypeBool, SimpleTypeDouble, SimpleTypeFloat, SimpleTypeInt8, SimpleTypeInt16, SimpleTypeInt32, SimpleTypeInt64, SimpleTypeUInt8, SimpleTypeUInt16, SimpleTypeUInt32, SimpleTypeUInt64}
var integerTypes = []SimpleType{SimpleTypeInt8, SimpleTypeInt16, SimpleTypeInt32, SimpleTypeInt64}
var unsignedIntegerTypes = []SimpleType{SimpleTypeUInt8, SimpleTypeUInt16, SimpleTypeUInt32, SimpleTypeUInt64}
var integerTypeCompatibilityRank map[SimpleType]int
var unsignedIntegerTypeCompatibilityRank map[SimpleType]int

// Initialize integerTypeCompatibilityRank and unsignedIntegerTypeCompatibilityRank
func init() {
	integerTypeCompatibilityRank = make(map[SimpleType]int)
	for i, t := range integerTypes {
		integerTypeCompatibilityRank[t] = i
	}
	unsignedIntegerTypeCompatibilityRank = make(map[SimpleType]int)
	for i, t := range unsignedIntegerTypes {
		unsignedIntegerTypeCompatibilityRank[t] = i
	}
}

// A SimpleType is a LiteralType:

func (SimpleType) LiteralTypeKind() TypeKind {
	return TypeKindSimple
}

// A SimpleType is a ConcreteType:

func (SimpleType) ConcreteTypeKind() TypeKind {
	return TypeKindSimple
}

// A SimpleType is a TypeRef:

func (SimpleType) TypeRefKind() TypeKind {
	return TypeKindSimple

}

// From interface TypeRef:

func (SimpleType) MarkUsedAsMapKey() bool {
	return true
}

func (SimpleType) MarkUsedAsConstantType() bool {
	return true
}

func (SimpleType) AllowedInNonZeroStructVersion() bool {
	return true
}

// From interface TypeRef:

// We allow users to assign integer literals to float or double variables as
// long as the integer literals are small enough that they can be guaranteed
// to be represented exactly.
const minFloatInt = -(1 << 24)
const maxFloatInt = (1 << 24)
const minDoubleInt = -(1 << 53)
const maxDoubleInt = (1 << 53)

func (t SimpleType) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	switch assignedValue := assignedValue.(type) {
	case LiteralValue:
		return t.isAssignmentCompatibleWithLiteral(assignedValue)
	case BuiltInConstantValue:
		return t.isAssignmentCompatibleWithBuiltIn(assignedValue)
	case *EnumValue:
		return false
	default:
		panic(fmt.Sprintf("Unexpected type %T.", t))
	}
}

func (t SimpleType) isAssignmentCompatibleWithLiteral(assignedValue LiteralValue) bool {
	if assignedValue.IsDefault() {
		// We don't support assigning "default" to a SimpleType variable.
		return false
	}
	if assignedValue.LiteralValueType() == StringLiteralType {
		// Not valid to assign a string to a SimpleType variable.
		return false
	}
	// Now we know the type of assignment is a literal type.
	assignedType := assignedValue.LiteralValueType().(SimpleType)
	if assignedType == SimpleTypeBool {
		// A bool literal may only be assigned to a bool variable.
		return t == SimpleTypeBool
	}
	// Now we know the type of assignment is a numeric literal type.
	switch t {
	case SimpleTypeBool:
		return false
	case SimpleTypeFloat:
		switch assignedType {
		case SimpleTypeFloat:
			return true
		case SimpleTypeDouble:
			int64Value := math.Abs(assignedValue.Value().(float64))
			return int64Value <= math.MaxFloat32
		default:
			int64Value, ok := int64Value(assignedValue)
			return ok && int64Value >= minFloatInt && int64Value <= maxFloatInt
		}
	case SimpleTypeDouble:
		switch assignedType {
		case SimpleTypeFloat, SimpleTypeDouble:
			return true
		default:
			int64Value, ok := int64Value(assignedValue)
			return ok && int64Value >= minDoubleInt && int64Value <= maxDoubleInt
		}
	case SimpleTypeInt8, SimpleTypeInt16, SimpleTypeInt32, SimpleTypeInt64:
		// A signed integer value may be assigned to a signed integer variable
		// whose type rank is at least as large.
		if rank, ok := integerTypeCompatibilityRank[assignedType]; ok {
			if rank <= integerTypeCompatibilityRank[t] {
				return true
			}
			return false
		}
		// An usigned integer value may be assigned to a signed integer variable
		// whose type rank is larger
		if rank, ok := unsignedIntegerTypeCompatibilityRank[assignedType]; ok {
			if rank < integerTypeCompatibilityRank[t] {
				return true
			}
			return false
		}
		return false
	case SimpleTypeUInt8, SimpleTypeUInt16, SimpleTypeUInt32, SimpleTypeUInt64:
		// An usigned integer value may be assigned to an unsigned integer variable
		// whose type rank is at least as large
		if rank, ok := unsignedIntegerTypeCompatibilityRank[assignedType]; ok {
			if rank <= unsignedIntegerTypeCompatibilityRank[t] {
				return true
			}
			return false
		}
		// A *non-negative* signed integer value may be assigned to an unsigned
		// integer variable whose type rank is at least as large.
		if rank, ok := integerTypeCompatibilityRank[assignedType]; ok {
			if rank <= unsignedIntegerTypeCompatibilityRank[t] {
				numericValue, _ := int64Value(assignedValue)
				return numericValue >= 0
			}
			return false
		}
		return false
	default:
		panic(fmt.Sprintf("Unexpected SimpleType: %s", t))
	}
}

func (t SimpleType) isAssignmentCompatibleWithBuiltIn(value BuiltInConstantValue) bool {
	switch t {
	case SimpleTypeBool:
		return false
	case SimpleTypeFloat:
		switch value {
		case FloatInfinity, FloatNegativeInfinity, FloatNAN:
			return true
		default:
			return false
		}
	case SimpleTypeDouble:
		return true
	default:
		return false
	}
}

func (t SimpleType) MarkTypeCompatible(assignment LiteralAssignment) bool {
	return t.isAssignmentCompatibleWithLiteral(assignment.assignedValue)
}

func (t SimpleType) SerializationSize() uint32 {
	switch t {
	case SimpleTypeBool:
		return 1
	case SimpleTypeDouble:
		return 8
	case SimpleTypeFloat:
		return 4
	case SimpleTypeInt8:
		return 1
	case SimpleTypeInt16:
		return 2
	case SimpleTypeInt32:
		return 4
	case SimpleTypeInt64:
		return 8
	case SimpleTypeUInt8:
		return 1
	case SimpleTypeUInt16:
		return 2
	case SimpleTypeUInt32:
		return 4
	case SimpleTypeUInt64:
		return 8
	default:
		panic(fmt.Sprintf("unexpected type: &d", t))
	}
}

func (t SimpleType) SerializationAlignment() uint32 {
	return t.SerializationSize()
}

func (SimpleType) NonAvoidableUserType() UserDefinedType {
	return nil
}

func (t SimpleType) String() string {
	switch t {
	case SimpleTypeBool:
		return "bool"
	case SimpleTypeDouble:
		return "double"
	case SimpleTypeFloat:
		return "float"
	case SimpleTypeInt8:
		return "int8"
	case SimpleTypeInt16:
		return "int16"
	case SimpleTypeInt32:
		return "int32"
	case SimpleTypeInt64:
		return "int64"
	case SimpleTypeUInt8:
		return "uint8"
	case SimpleTypeUInt16:
		return "uint16"
	case SimpleTypeUInt32:
		return "uint32"
	case SimpleTypeUInt64:
		return "uint64"
	default:
		panic(fmt.Sprintf("unexpected type: &d", t))
	}
}

func (t SimpleType) TypeName() string {
	return t.String()
}

/////////////////////////////////////////////////////////////
//String Type
/////////////////////////////////////////////////////////////

type StringType struct {
	// When used as a type reference indicates whether or not
	// the reference is nullable. When used as a type this
	// value is ignored.
	nullable bool
}

func (s StringType) Nullable() bool {
	return s.nullable
}

func (s StringType) AllowedInNonZeroStructVersion() bool {
	return s.nullable
}

func (StringType) SerializationSize() uint32 {
	return 8
}

func (StringType) SerializationAlignment() uint32 {
	return 8
}

func (StringType) NonAvoidableUserType() UserDefinedType {
	return nil
}

// StringLiteralType is a global singleton representing the unique LiteralType string.
var StringLiteralType LiteralType = StringType{}

// A StringType is a LiteralType:

func (StringType) LiteralTypeKind() TypeKind {
	return TypeKindString
}

// A StringType is a ConcreteType:

func (StringType) ConcreteTypeKind() TypeKind {
	return TypeKindString
}

// A StringType is a TypeRef:

func (StringType) TypeRefKind() TypeKind {
	return TypeKindString
}

func (StringType) MarkUsedAsMapKey() bool {
	return true
}

func (StringType) MarkUsedAsConstantType() bool {
	return true
}

func (StringType) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	switch assignedValue := assignedValue.(type) {
	case LiteralValue:
		return assignedValue.LiteralValueType() == StringLiteralType && !assignedValue.IsDefault()
	case BuiltInConstantValue:
		return false
	case *EnumValue:
		return false
	default:
		panic(fmt.Sprintf("Unexpected type %T.", assignedValue))
	}
}

func (s StringType) MarkTypeCompatible(assignment LiteralAssignment) (ok bool) {
	return s.IsAssignmentCompatible(assignment.assignedValue)
}

func (s StringType) String() string {
	nullableSpecifier := ""
	if s.nullable {
		nullableSpecifier = "?"
	}
	return fmt.Sprintf("string%s", nullableSpecifier)
}

func (s StringType) TypeName() string {
	return s.String()
}

/////////////////////////////////////////////////////////////
// Handle Type
/////////////////////////////////////////////////////////////

type HandleKind int

const (
	HandleKindUnspecified HandleKind = iota
	HandleKindChannel
	HandleKindVmo
	HandleKindProcess
	HandleKindThread
	HandleKindEvent
	HandleKindPort
	HandleKindJob
	HandleKindSocket
	HandleKindEventPair
)

var allHandleKinds = []HandleKind{
	HandleKindUnspecified,
	HandleKindChannel,
	HandleKindVmo,
	HandleKindProcess,
	HandleKindThread,
	HandleKindEvent,
	HandleKindPort,
	HandleKindJob,
	HandleKindSocket,
	HandleKindEventPair,
}

// HandleTypeRef is only ever used to represent type references, never types.
type HandleTypeRef struct {
	nullable bool

	kind HandleKind
}

func (h HandleTypeRef) Nullable() bool {
	return h.nullable
}

func (h HandleTypeRef) AllowedInNonZeroStructVersion() bool {
	return h.nullable
}

func (HandleTypeRef) SerializationSize() uint32 {
	return 4
}

func (HandleTypeRef) SerializationAlignment() uint32 {
	return 4
}

func (HandleTypeRef) NonAvoidableUserType() UserDefinedType {
	return nil
}

func (h HandleTypeRef) HandleKind() HandleKind {
	return h.kind
}

func (HandleTypeRef) TypeRefKind() TypeKind {
	return TypeKindHandle
}

func (HandleTypeRef) MarkUsedAsMapKey() bool {
	return false
}

func (HandleTypeRef) MarkUsedAsConstantType() bool {
	return false
}

func (HandleTypeRef) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	// Assignments to HandleTypeRef variables are never validated after the parsing phase
	// because they are validated during the parsing phase. Therefore there is never
	// a need to call this method.
	panic("This method should never be invoked.")
}

func (HandleTypeRef) MarkTypeCompatible(assignment LiteralAssignment) bool {
	return false
}

func (h HandleTypeRef) String() string {
	suffix := ""
	switch h.kind {
	case HandleKindUnspecified:
		break
	case HandleKindChannel:
		suffix = "<channel>"
	case HandleKindVmo:
		suffix = "<vmo>"
	case HandleKindProcess:
		suffix = "<process>"
	case HandleKindThread:
		suffix = "<thread>"
	case HandleKindEvent:
		suffix = "<event>"
	case HandleKindPort:
		suffix = "<port>"
	case HandleKindJob:
		suffix = "<job>"
	case HandleKindSocket:
		suffix = "<socket>"
	case HandleKindEventPair:
		suffix = "<eventpair>"
	default:
		panic(fmt.Sprintf("Unrecognized handle kind %d", h.kind))
	}
	nullable := ""
	if h.nullable {
		nullable = "?"
	}
	return fmt.Sprintf("%s%s%s", "handle", suffix, nullable)
}

func (h HandleTypeRef) TypeName() string {
	return h.String()
}

/////////////////////////////////////////////////////////////
//
// Built-In Types
//
// The built-in types are defined to be those for which a type reference is
// does not require any resolution. These are the simple types, string,
// the handle types and their nullable variants.
//
// We make a map of all built-in types by name.  The values of the
// map are of type TypeRef because this map is only used in the
// context of type references, not types.
/////////////////////////////////////////////////////////////

var allBuiltInTypes []TypeRef
var builtInTypeMap map[string]TypeRef

// Initialize allBuiltInTypes and TypeRefMap
func init() {
	allBuiltInTypes = make([]TypeRef, len(allSimpleTypes)+len(allHandleKinds)*2+2)
	// Add the simple types
	for i, t := range allSimpleTypes {
		allBuiltInTypes[i] = t
	}

	// Add the string types
	i := len(allSimpleTypes)
	allBuiltInTypes[i] = StringType{false}
	i++
	allBuiltInTypes[i] = StringType{true}
	i++

	// Add the handle types
	for _, kind := range allHandleKinds {
		allBuiltInTypes[i] = HandleTypeRef{false, kind}
		i++
		allBuiltInTypes[i] = HandleTypeRef{true, kind}
		i++
	}

	builtInTypeMap = make(map[string]TypeRef, len(allBuiltInTypes))
	for _, t := range allBuiltInTypes {
		builtInTypeMap[t.String()] = t
	}
}

func BuiltInType(name string) TypeRef {
	return builtInTypeMap[name]
}

/////////////////////////////////////////////////////////////
// ArrayTypeRef
/////////////////////////////////////////////////////////////

// ArrayTypeRef is only ever used to represent type references, never types.
type ArrayTypeRef struct {
	nullable bool

	// If fixed_length < 0 then the array does not have a fixed length;
	fixedLength int32

	elementType TypeRef
}

func NewArrayTypeRef(elementType TypeRef, fixedSize int32, nullable bool) *ArrayTypeRef {
	return &ArrayTypeRef{nullable, fixedSize, elementType}
}

func (a ArrayTypeRef) Nullable() bool {
	return a.nullable
}

func (a ArrayTypeRef) AllowedInNonZeroStructVersion() bool {
	return a.nullable
}

func (ArrayTypeRef) SerializationSize() uint32 {
	return 8
}

func (ArrayTypeRef) SerializationAlignment() uint32 {
	return 8
}

func (a ArrayTypeRef) FixedLength() int32 {
	return a.fixedLength
}

func (a ArrayTypeRef) ElementType() TypeRef {
	return a.elementType
}

func (a ArrayTypeRef) NonAvoidableUserType() UserDefinedType {
	if a.nullable || a.fixedLength <= 0 {
		return nil
	}
	return a.elementType.NonAvoidableUserType()
}

// An ArrayTypeRef is a TypeRef:

func (ArrayTypeRef) TypeRefKind() TypeKind {
	return TypeKindArray
}

func (ArrayTypeRef) MarkUsedAsMapKey() bool {
	return false
}

func (ArrayTypeRef) MarkUsedAsConstantType() bool {
	return false
}

func (ArrayTypeRef) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	// Assignments to ArrayTypeRef variables are never validated after the parsing phase
	// because they are validated during the parsing phase. Therefore there is never
	// a need to call this method.
	panic("This method should never be invoked.")
}

func (ArrayTypeRef) MarkTypeCompatible(assignment LiteralAssignment) bool {
	return false
}

func (a ArrayTypeRef) ToString(debug bool) string {
	fixedLengthSpecifier := ""
	if a.fixedLength > 0 {
		fixedLengthSpecifier = fmt.Sprintf(", %d", a.fixedLength)
	}
	nullableSpecifier := ""
	if a.nullable {
		nullableSpecifier = "?"
	}
	var elementTypeString string
	if debug {
		elementTypeString = a.elementType.String()
	} else {
		elementTypeString = a.elementType.TypeName()
	}
	return fmt.Sprintf("array<%s%s>%s", elementTypeString, fixedLengthSpecifier, nullableSpecifier)
}

func (a ArrayTypeRef) String() string {
	return a.ToString(true)
}

func (a ArrayTypeRef) TypeName() string {
	return a.ToString(false)
}

/////////////////////////////////////////////////////////////
// MapTypeRef
/////////////////////////////////////////////////////////////

// MapTypeRef is only ever used to represent type references, never types.
type MapTypeRef struct {
	nullable bool

	/// The key_type must be a simple type, a string or an enum type.
	keyType   TypeRef
	valueType TypeRef
}

func NewMapTypeRef(keyType TypeRef, valueType TypeRef, nullable bool) *MapTypeRef {
	return &MapTypeRef{nullable, keyType, valueType}
}

func (m MapTypeRef) Nullable() bool {
	return m.nullable
}

func (m MapTypeRef) AllowedInNonZeroStructVersion() bool {
	return m.nullable
}

func (MapTypeRef) SerializationSize() uint32 {
	return 8
}

func (MapTypeRef) SerializationAlignment() uint32 {
	return 8
}

func (MapTypeRef) NonAvoidableUserType() UserDefinedType {
	return nil
}

func (m MapTypeRef) KeyType() TypeRef {
	return m.keyType
}

func (m MapTypeRef) ValueType() TypeRef {
	return m.valueType
}

// A MapTypeRef is a TypeRef:

func (MapTypeRef) TypeRefKind() TypeKind {
	return TypeKindMap
}

func (MapTypeRef) MarkUsedAsMapKey() bool {
	return false
}

func (MapTypeRef) MarkUsedAsConstantType() bool {
	return false
}

func (MapTypeRef) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	// Assignments to MapTypeRef variables are never validated after the parsing phase
	// because they are validated during the parsing phase. Therefore there is never
	// a need to call this method.
	panic("This method should never be invoked.")
}

func (MapTypeRef) MarkTypeCompatible(assignment LiteralAssignment) bool {
	return false
}

func (m MapTypeRef) ToString(debug bool) string {
	nullableSpecifier := ""
	if m.nullable {
		nullableSpecifier = "?"
	}
	var keyTypeString, valueTypeString string
	if debug {
		keyTypeString = m.keyType.String()
		valueTypeString = m.valueType.String()
	} else {
		keyTypeString = m.keyType.TypeName()
		valueTypeString = m.valueType.TypeName()
	}
	return fmt.Sprintf("map<%s, %s>%s", keyTypeString, valueTypeString, nullableSpecifier)
}

func (m MapTypeRef) String() string {
	return m.ToString(true)
}

func (m MapTypeRef) TypeName() string {
	return m.ToString(false)
}

/////////////////////////////////////////////////////////////
// UserTypeRef
//
// A UserTypeRef represents an identifier that refers to
// a user-defined type: an interface, struct, union or enum.
/////////////////////////////////////////////////////////////

type UserTypeRef struct {
	nullable bool

	interfaceRequest bool

	// The scope where this type reference occurred. This is
	// used to resolve the identifier.
	scope *Scope

	// The type identifier as it appears at the reference site.
	identifier string

	// The first token of the type reference. This is used to help
	// generate user-facing error messages during resolution or later.
	token lexer.Token

	// We record the ways in which this reference is being used. After the
	// reference is resolved we will check whether or not the type being
	// referenced is allowed to be used in these ways.
	usedAsMapKey       bool
	usedAsConstantType bool
	literalAssignment  *LiteralAssignment

	// After this reference is resolved this value will be non-nil.
	resolvedType UserDefinedType
}

func NewUserTypeRef(identifier string, nullable bool,
	interfaceRequest bool, scope *Scope, token lexer.Token) *UserTypeRef {
	return &UserTypeRef{identifier: identifier,
		nullable: nullable, interfaceRequest: interfaceRequest,
		scope: scope, token: token}
}

// NewResolvedUserTypeRef() creates a new UserTypeRef that is pre-resolved.
// An example of where we need this is when creating an enum value initializer.
// We need a UserTypeRef to act as the "asignee type" for the value, but we
// already know the resolved type because the assignment occurs in the
// lexical scope of an enum declaration.
func NewResolvedUserTypeRef(identifier string, resolvedType UserDefinedType) *UserTypeRef {
	return &UserTypeRef{identifier: identifier, resolvedType: resolvedType}
}

func (t *UserTypeRef) Nullable() bool {
	return t.nullable
}

func (t *UserTypeRef) AllowedInNonZeroStructVersion() bool {
	if t.resolvedType == nil {
		panic("This method should only be invoked after successful resolution.")
	}
	if t.resolvedType.Kind() == UserDefinedTypeKindEnum {
		return true
	}
	return t.nullable
}

func (t *UserTypeRef) SerializationSize() uint32 {
	if t.resolvedType == nil {
		panic("This method should only be invoked after successful resolution.")
	}
	if t.IsInterfaceRequest() {
		return HandleTypeRef{}.SerializationSize()
	}
	return t.resolvedType.SerializationSize()
}

func (t *UserTypeRef) SerializationAlignment() uint32 {
	if t.resolvedType == nil {
		panic("This method should only be invoked after successful resolution.")
	}
	return t.resolvedType.SerializationAlignment()
}

func (t *UserTypeRef) NonAvoidableUserType() UserDefinedType {
	if t.nullable || t.interfaceRequest {
		return nil
	}
	if t.resolvedType == nil {
		panic("This method should only be invoked after successful resolution.")
	}
	switch udt := t.resolvedType.(type) {
	case *MojomStruct, *MojomUnion:
		return udt
	}
	return nil
}

func (t *UserTypeRef) IsInterfaceRequest() bool {
	return t.interfaceRequest
}

func (t *UserTypeRef) Identifier() string {
	return t.identifier
}

// A UserTypeRef is a TypeRef:

func (UserTypeRef) TypeRefKind() TypeKind {
	return TypeKindUserDefined
}

func (t *UserTypeRef) ResolvedType() UserDefinedType {
	return t.resolvedType
}

func (t *UserTypeRef) MarkUsedAsMapKey() bool {
	t.usedAsMapKey = true
	return true
}

func (t *UserTypeRef) MarkUsedAsConstantType() bool {
	t.usedAsConstantType = true
	return true
}

func (ref *UserTypeRef) IsAssignmentCompatible(assignedValue ConcreteValue) bool {
	if ref.resolvedType == nil {
		panic("This method should only be invoked after successful resolution.")
	}
	switch assignedValue := assignedValue.(type) {
	case LiteralValue:
		return ref.resolvedType.IsAssignmentCompatibleWith(assignedValue)
	case BuiltInConstantValue:
		return false
	case *EnumValue:
		return ref.resolvedType == assignedValue.EnumType()
	default:
		panic(fmt.Sprintf("Unexpected type %T.", assignedValue))
	}
}

func (t *UserTypeRef) MarkTypeCompatible(assignment LiteralAssignment) bool {
	// Just mark the assignment attempt and return true. We will validate it
	// during the validation phase.
	t.literalAssignment = &assignment
	return true
}

func (ref *UserTypeRef) validateAfterResolution() error {
	var file *MojomFile = nil
	if ref.scope != nil && ref.scope.file != nil {
		file = ref.scope.file
	}

	if ref.IsInterfaceRequest() && ref.resolvedType.Kind() != UserDefinedTypeKindInterface {
		message := fmt.Sprintf("Invalid interface request specification: %s. %s is not an interface type.",
			ref.TypeName(), ref.ResolvedType().FullyQualifiedName())
		message = UserErrorMessage(file, ref.token, message)
		return fmt.Errorf(message)
	}

	if ref.Nullable() && ref.resolvedType.Kind() == UserDefinedTypeKindEnum {
		message := fmt.Sprintf("The type %s is invalid because %s is an enum type and these may not be made nullable.",
			ref.TypeName(), ref.ResolvedType().FullyQualifiedName())
		message = UserErrorMessage(file, ref.token, message)
		return fmt.Errorf(message)
	}

	if ref.resolvedType.Kind() != UserDefinedTypeKindEnum {
		// A type ref has resolved to a non-enum type. Make sure it is not
		// being used as either a map key or a constant declaration. Also
		// make sure that a literal was not assigned to it.
		if ref.usedAsMapKey {
			message := fmt.Sprintf("The type %s is not allowed as the key type of a map. "+
				"Only simple types, strings and enum types may be map keys.", ref.TypeName())
			message = UserErrorMessage(file, ref.token, message)
			return fmt.Errorf(message)
		}
		if ref.usedAsConstantType {
			message := fmt.Sprintf("The type %s is not allowed as the type of a declared constant. "+
				"Only simple types, strings and enum types may be used.", ref.TypeName())
			message = UserErrorMessage(file, ref.token, message)
			return fmt.Errorf(message)
		}
	}
	if ref.literalAssignment != nil && !ref.resolvedType.IsAssignmentCompatibleWith(ref.literalAssignment.assignedValue) {
		var message string
		if ref.literalAssignment.assignedValue.IsDefault() {
			message = fmt.Sprintf("Illegal assignment: The 'default' keyword may not be used with the field %s of type %s.",
				ref.literalAssignment.variableName, ref.TypeName())
		} else {
			message = fmt.Sprintf("Illegal assignment: %s %s of type %s may not be assigned the value %v of type %s.",
				ref.literalAssignment.kind, ref.literalAssignment.variableName,
				ref.TypeName(), ref.literalAssignment.assignedValue,
				ref.literalAssignment.assignedValue.LiteralValueType())
		}
		message = UserErrorMessage(file, ref.token, message)
		return fmt.Errorf(message)
	}

	return nil
}

func (t *UserTypeRef) ToString(debug bool) string {
	interfaceRequest := ""
	if t.interfaceRequest {
		interfaceRequest = "&"
	}
	nullable := ""
	if t.nullable {
		nullable = "?"
	}
	debugString := ""
	if debug {
		resolvedKey := ""
		if t.resolvedType != nil {
			resolvedKey = t.resolvedType.TypeKey()
		}
		debugString = fmt.Sprintf("(%s)", resolvedKey)
	}
	baseName := t.identifier
	if !debug && t.resolvedType != nil {
		baseName = t.resolvedType.FullyQualifiedName()
	}
	return fmt.Sprintf("%s%s%s%s", debugString, baseName, interfaceRequest, nullable)
}

func (t *UserTypeRef) String() string {
	return t.ToString(true)
}

func (t *UserTypeRef) TypeName() string {
	return t.ToString(false)
}

func (t *UserTypeRef) LongString() string {
	return fmt.Sprintf("%s %s:%s. (In %s.)", t.identifier,
		t.scope.file.CanonicalFileName, t.token.ShortLocationString(), t.scope)
}

/////////////////////////////////////////////////////////////
// ValueRef
/////////////////////////////////////////////////////////////

// ValueRef represents an occurrence in the .mojom file of a
// reference to a value. These occur as the default values of fields,
// as the values of declared constants, and as the explicitly assigned value of
// an enum value.  A ValueRef is either a LiteralValue or a UserValueRef.
type ValueRef interface {
	ResolvedConcreteValue() ConcreteValue
	MarkUsedAsEnumValueInitializer() bool
}

// UserValueRef represents a reference to a user-defined value. That is, a
// reference to an EnumValue or a UserDefinedConstant.
type UserValueRef struct {

	// The scope in which the reference occurs. This is necessary in order
	// to resolve the reference.
	scope *Scope

	// The first token that the parser associates with the value reference.
	token lexer.Token

	// The identifier as it appears in the text.
	identifier string

	usedAsEnumValueInitializer bool

	// A value reference always occurs in the context of some
	// assignment. This may be the assignment of a default value
	// to a field, the assignment of a value to a declared constant,
	// or the assignment of an integer to an enum value. In all cases we
	// know at the site of the assignment what the name and  declared type of
	// the assignee is and we record that here. After the UserValueRef
	// has been resolved we will check that the type of |resolvedValue|
	// is compatible with the assignee.
	assigneeSpec AssigneeSpec

	// The user-defined constant or enum value or BuiltInConstantValue that the
	// reference resolves to.
	resolvedDeclaredValue UserDefinedValue

	// The concrete value that the reference resolves to.
	// If |resolvedDeclaredValue| is an EnumValue then that is already
	// a ConcreteValue and |resolvedConcreteValue| == |resolvedDeclaredValue|.
	// But if |resolvedDeclaredValue| is a UserDefinedConstant then,
	// since those are not considered ConcreteValues,
	// |resolvedConcreteValue| is that constant's resolved value.
	resolvedConcreteValue ConcreteValue
}

// AssigneeSpec is used to store the name and type of the left-hand-side of
// a variable assignment. It is used when the right-hand-side is a reference
// to a value. After the reference has been resolved we validate the resolved
// value against the AssigneeSpec.
type AssigneeSpec struct {
	Name string
	Type TypeRef
}

func (v UserValueRef) Identifier() string {
	return v.identifier
}

func (v UserValueRef) ResolvedConcreteValue() ConcreteValue {
	return v.resolvedConcreteValue
}

func (v UserValueRef) ResolvedDeclaredValue() UserDefinedValue {
	return v.resolvedDeclaredValue
}

func (v *UserValueRef) MarkUsedAsEnumValueInitializer() bool {
	v.usedAsEnumValueInitializer = true
	return true
}

func (v *UserValueRef) validateAfterResolution() error {
	if v.resolvedConcreteValue == nil {
		// Panic because this method should only be invoked after successful resolution.
		panic(fmt.Sprintf("Unresolved concrete value for %v.", v))
	}
	if v.usedAsEnumValueInitializer {
		switch concreteValue := v.resolvedConcreteValue.(type) {
		case LiteralValue:
			if _, ok := int32Value(concreteValue); !ok {
				message := fmt.Sprintf("Illegal assignment: %s cannot be used as an enum value initializer because "+
					"its value, %v, is not a signed 32-bit integer.",
					v.identifier, concreteValue)
				message = UserErrorMessage(v.scope.file, v.token, message)
				return fmt.Errorf(message)
			}
		case BuiltInConstantValue:
			message := fmt.Sprintf("Illegal assignment: %s cannot be used as an enum value initializer.", v.identifier)
			message = UserErrorMessage(v.scope.file, v.token, message)
			return fmt.Errorf(message)
		case *EnumValue:
			// An EnumValue is being used as an EnumValue initializer.
			// We will only check that the two EnumTypes match.
			// In ComputeEnumValueIntegers() in computed_data.go we will further validate.
			if !v.assigneeSpec.Type.IsAssignmentCompatible(v.resolvedConcreteValue) {
				var message string
				switch v.resolvedDeclaredValue.(type) {
				case *EnumValue:
					// An enum value is being used directly as an initializer.
					message = fmt.Sprintf("Illegal assignment: The enum value %s of type %s may not be used as an initializer for %s of type %s.",
						v.identifier, concreteValue.enumType.fullyQualifiedName, v.assigneeSpec.Name, v.assigneeSpec.Type.TypeName())
				default:
					// A user-defined constant whose value is an enum value is being used as an initializer.
					message = fmt.Sprintf("Illegal assignment: %s with the value %v may not be used as an initializer for %s of type %s.",
						v.identifier, concreteValue.fullyQualifiedName, v.assigneeSpec.Name, v.assigneeSpec.Type.TypeName())
				}
				return fmt.Errorf(UserErrorMessage(v.scope.file, v.token, message))
			}
		default:
			panic(fmt.Sprintf("Unexpected type %T", concreteValue))
		}
		return nil
	}

	if !v.assigneeSpec.Type.IsAssignmentCompatible(v.resolvedConcreteValue) {
		var message string
		switch assigneeType := v.assigneeSpec.Type.(type) {
		case SimpleType, StringType, *UserTypeRef:
			switch concreteValue := v.resolvedConcreteValue.(type) {
			case LiteralValue:
				// A user-defined constant whose value is a literal value is being assigned to a variable.
				message = fmt.Sprintf("Illegal assignment: %s with the value %v of type %s may not be assigned to %s of type %s.",
					v.identifier, concreteValue, concreteValue.LiteralValueType(), v.assigneeSpec.Name, assigneeType.TypeName())
			case BuiltInConstantValue:
				switch v.resolvedDeclaredValue.(type) {
				case BuiltInConstantValue:
					// A built-in float constant is being assigned directly to a variable.
					message = fmt.Sprintf("Illegal assignment: %s may not be assigned to %s of type %s.",
						v.identifier, v.assigneeSpec.Name, assigneeType.TypeName())
				default:
					// A user-defined constant whose value is a built-in float constant is being assigned to a variable.
					message = fmt.Sprintf("Illegal assignment: %s with the value %v may not be assigned to %s of type %s.",
						v.identifier, concreteValue, v.assigneeSpec.Name, assigneeType.TypeName())
				}
			case *EnumValue:
				switch v.resolvedDeclaredValue.(type) {
				case *EnumValue:
					// An enum value is being assigned directly to a variable.
					message = fmt.Sprintf("Illegal assignment: The enum value %s of type %s may not be assigned to %s of type %s.",
						v.identifier, concreteValue.enumType.fullyQualifiedName, v.assigneeSpec.Name, assigneeType.TypeName())
				default:
					// A user-defined constant whose value is an enum value is being assigned to a variable.
					message = "Illegal assignment: %s with the value %v may not be assigned to %s of type %s."
					if v.usedAsEnumValueInitializer {
						// A user-defined constant whose value is an enum value is being used as an initializer.
						message = "Illegal assignment: %s with the value %v may not be used as an initializer for %s of type %s."
					}
					message = fmt.Sprintf(message, v.identifier, concreteValue.fullyQualifiedName, v.assigneeSpec.Name, assigneeType.TypeName())
				}
			default:
				panic(fmt.Sprintf("Unexpected type %T", concreteValue))
			}
		default:
			panic(fmt.Sprintf("Unexpected type %T", assigneeType))
		}
		return fmt.Errorf(UserErrorMessage(v.scope.file, v.token, message))
	}
	return nil
}

func (v *UserValueRef) String() string {
	resolvedKey := ""
	if v.resolvedDeclaredValue != nil {
		resolvedKey = v.resolvedDeclaredValue.ValueKey()
	}
	return fmt.Sprintf("(%s)%s", resolvedKey, v.identifier)
}

func (v *UserValueRef) LongString() string {
	return fmt.Sprintf("%s %s:%s. (In %s.)", v.identifier,
		v.scope.file.CanonicalFileName, v.token.ShortLocationString(), v.scope)
}

func NewUserValueRef(assigneeSpec AssigneeSpec, identifier string, scope *Scope,
	token lexer.Token) *UserValueRef {
	valueReference := new(UserValueRef)
	valueReference.assigneeSpec = assigneeSpec
	valueReference.scope = scope
	valueReference.token = token
	valueReference.identifier = identifier

	return valueReference
}

/////////////////////////////////////////////////////////////
// Concrete Values
/////////////////////////////////////////////////////////////

// A ConcreteValue is a LiteralValue, a *EnumValue, or a BuiltInConstantValue.
type ConcreteValue interface {
	ValueType() ConcreteType
	Value() interface{}
}

/////////////////////////////////////////////////////////////
// Literal Values
/////////////////////////////////////////////////////////////

// LiteralValue represents a string, number or boolean literal.
// The LiteralValue struct implements both ValueRef and ConcreteValue.
// This reflects the fact that a literal value is already resolved.
type LiteralValue struct {
	// The Type must be simple or string
	valueType LiteralType

	value interface{}

	// Does this LiteralValue represent the pseudo value "default"
	isDefault bool

	// The token that represents the literal value in the source code.
	token *lexer.Token
}

// int64Value returns the value of |literalValue| as an int64.
// If the type of |literalValue| is not an integer type then |ok|
// will be false and |int64Value| will be zero. If the type of
// |literalValue| is any of the integer types smaller the uint64 then
// |ok| will be true and |int64Value| will be the integer value cast
// to an |int64|. If the type of |literalValue| is uint64 then
// |int64Value| will be the integer value cast to an int64 and
// |ok| will indicate whether or not the pre-cast value is <= MaxInt64.
func int64Value(literalValue LiteralValue) (int64Value int64, ok bool) {
	value := literalValue.Value()
	switch literalValue.valueType {
	case SimpleTypeInt8:
		return int64(value.(int8)), true
	case SimpleTypeUInt8:
		return int64(value.(uint8)), true
	case SimpleTypeInt16:
		return int64(value.(int16)), true
	case SimpleTypeUInt16:
		return int64(value.(uint16)), true
	case SimpleTypeInt32:
		return int64(value.(int32)), true
	case SimpleTypeUInt32:
		return int64(value.(uint32)), true
	case SimpleTypeInt64:
		return value.(int64), true
	case SimpleTypeUInt64:
		numericValue := value.(uint64)
		ok := numericValue <= math.MaxInt64
		return int64(numericValue), ok
	}
	return 0, false
}

// int32Value returns the value of |literalValue| as an int32.
// If the type of |literalValue| is not an integer type then |ok|
// will be false and |int32Value| will be zero. If the type of
// |literalValue| is any of the integer types then |int32Value| will
// be the integer value cast to an int32 and |ok| will indicate whether
// or not the pre-cast value is between math.MinInt32 and math.MaxInt32.
func int32Value(literalValue LiteralValue) (int32Value int32, ok bool) {
	int64Value, ok := int64Value(literalValue)
	ok = ok && int64Value <= math.MaxInt32 && int64Value >= math.MinInt32
	int32Value = int32(int64Value)
	return
}

// uint32Value returns the value of |literalValue| as an uint32.
// If the type of |literalValue| is not an integer type then |ok|
// will be false and |int32Value| will be zero. If the type of
// |literalValue| is any of the integer types then |uint32Value| will
// be the integer value cast to an uint32 and |ok| will indicate whether
// or not the pre-cast value is between 0 and math.MaxUInt32.
func uint32Value(literalValue LiteralValue) (int32Value uint32, ok bool) {
	int64Value, ok := int64Value(literalValue)
	ok = ok && int64Value <= math.MaxUint32 && int64Value >= 0
	int32Value = uint32(int64Value)
	return
}

func MakeStringLiteralValue(text string, token *lexer.Token) LiteralValue {
	return LiteralValue{StringLiteralType, text, false, token}
}

func MakeBoolLiteralValue(value bool, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeBool, value, false, token}
}

func MakeInt8LiteralValue(value int8, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeInt8, value, false, token}
}

func MakeInt16LiteralValue(value int16, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeInt16, value, false, token}
}
func MakeInt32LiteralValue(value int32, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeInt32, value, false, token}
}

func MakeInt64LiteralValue(value int64, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeInt64, value, false, token}
}

func MakeUint8LiteralValue(value uint8, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeUInt8, value, false, token}
}
func MakeUint16LiteralValue(value uint16, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeUInt16, value, false, token}
}
func MakeUint32LiteralValue(value uint32, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeUInt32, value, false, token}
}

func MakeUint64LiteralValue(value uint64, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeUInt64, value, false, token}
}

func MakeDoubleLiteralValue(value float64, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeDouble, value, false, token}
}

func MakeFloatLiteralValue(value float32, token *lexer.Token) LiteralValue {
	return LiteralValue{SimpleTypeFloat, value, false, token}
}

func MakeDefaultLiteral(token *lexer.Token) LiteralValue {
	return LiteralValue{StringLiteralType, "default", true, token}
}

func (lv LiteralValue) String() string {
	if lv.isDefault {
		return "default"
	}
	switch lv.valueType.ConcreteTypeKind() {
	case TypeKindString:
		return fmt.Sprintf("%q", lv.value)
	default:
		return fmt.Sprintf("%v", lv.value)
	}
}

func (lv LiteralValue) MarkUsedAsEnumValueInitializer() bool {
	// A literal value is legal as an enum value initializer just in case
	// it is a signed 32-bit integer value.
	_, ok := int32Value(lv)
	return ok
}

func (lv LiteralValue) LiteralValueType() LiteralType {
	return lv.valueType
}

// A LiteralValue is a ConcreteValue.
func (lv LiteralValue) ValueType() ConcreteType {
	return lv.valueType
}
func (v LiteralValue) Value() interface{} {
	return v.value
}

func (v LiteralValue) Token() *lexer.Token {
	return v.token
}

// A LiteralValue is also a ValueRef and is its own
// ResolvedValue:

func (v LiteralValue) ResolvedConcreteValue() ConcreteValue {
	return v
}

func (v LiteralValue) IsDefault() bool {
	return v.isDefault
}

// LiteralAssignment represents an assignment of a literal value to a variable.
// It is used to help track of whether or not the assignment is valid and if not
// to issue an appropriate error message.
type LiteralAssignmentKind int

const (
	LiteralAssignmentKindDefaultStructField LiteralAssignmentKind = iota
	LiteralAssignmentKindConstantDeclaration
)

func (k LiteralAssignmentKind) String() string {
	switch k {
	case LiteralAssignmentKindDefaultStructField:
		return "Field"
	case LiteralAssignmentKindConstantDeclaration:
		return "Const"
	default:
		panic(fmt.Sprintf("Unknown LiteralAssignmentKind %d", k))
	}
}

type LiteralAssignment struct {
	assignedValue LiteralValue
	variableName  string
	kind          LiteralAssignmentKind
}
