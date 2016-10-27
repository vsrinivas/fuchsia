// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"bytes"
	"errors"
	"fmt"
	"math"
	"mojom/mojom_tool/lexer"
	"mojom/mojom_tool/utils"
	"sort"
	"strings"
)

/*
This file contains data structures and functions used to describe user-defined,
types (i.e. structs, unions, enums and interfaces) and user-defined values
(i.e. declared constants and enum values.) See the comments at the top of
types.go for a discussion of types vs type references and values vs
value references.
*/

// User-Defined Type Kinds
type UserDefinedTypeKind int

const (
	UserDefinedTypeKindStruct UserDefinedTypeKind = iota
	UserDefinedTypeKindInterface
	UserDefinedTypeKindEnum
	UserDefinedTypeKindUnion
)

func (k UserDefinedTypeKind) String() string {
	switch k {
	case UserDefinedTypeKindStruct:
		return "struct"
	case UserDefinedTypeKindInterface:
		return "interface"
	case UserDefinedTypeKindEnum:
		return "enum"
	case UserDefinedTypeKindUnion:
		return "union"
	default:
		panic(fmt.Sprintf("Unknown UserDefinedTypeKind: %d", k))
	}
}

// A DeclaredObject is anything that can be registered in a scope:
// A UserDefinedType, a UserDefinedValue, a method or a struct field.
type DeclaredObject interface {
	MojomElement
	Attributes() *Attributes
	SimpleName() string
	NameToken() lexer.Token
	FullyQualifiedName() string
	KindString() string
	Scope() *Scope
	RegisterInScope(scope *Scope) DuplicateNameError
}

func FullLocationString(o DeclaredObject) string {
	return fmt.Sprintf("%s:%s", o.Scope().file.CanonicalFileName,
		o.NameToken().ShortLocationString())
}

/////////////////////////////////////////////////////////////
// The UserDefinedType interface. This is implemented by
// MojomStruct, MojomInterface, MojomEnum and MojomUnion
/////////////////////////////////////////////////////////////
type UserDefinedType interface {
	DeclaredObject
	DeclaredObjectsContainer
	Kind() UserDefinedTypeKind
	TypeKey() string
	IsAssignmentCompatibleWith(value LiteralValue) bool

	// ComputeFinalData() is invoked on each user-defined type in a MojomDescriptor
	// after the resolution and type validation phases have completed successfully.
	// The method computes information that is useful for the code generators in the
	// backend. Examples include struct field packing data and MinVersion values.
	// See computed_data.go.
	ComputeFinalData() error

	// CheckWellFounded() is invoked on each user-defined type in a MojomDescriptor
	// after the resolution and type validation phases have completed successfully.
	// The method performs an analysis of the type graph below a given type in order
	// to detect ill-founded types. An ill-founded type is one for which it is impossible
	// to create an instance that would be legally serializable using Mojo
	// serialization. This method returns a non-nil error just in case an
	// ill-founded type is detected. The contained error message is appropriate
	// for display to an end-user.
	//
	// An example of an ill-founded type is a struct |Foo| with a field whose type
	// is a non-nullable Foo. Thus ill-foundedness may be due to a cycle in the
	// type graph. But not all cycles cause ill-foundedness. Firstly nullable
	// fields do not lead to ill-foundedness as the potential cycle can be broken
	// by setting the field to null. Also the situation with unions is more complicated:
	// Type graphs involving unions are only ill-founded if every possible way of
	// chosing a value for all of the unions still leads to an unbreakable cycle.
	//
	// We model this using the notion of a well-founded two-sorted graph. See
	// utils/well-founded_graphs.go. Using the terminology from that file, we
	// model structs as circle nodes and unions as square nodes. In this context
	// the type graph only includes edges for non-nullable fields and arrays of
	// fixed positive length.
	CheckWellFounded() error

	// SerializationSize() returns the number of bytes necessary to serialize an
	// instance of this type in Mojo serialization.
	SerializationSize() uint32

	// SerializationAlignment() returns the number of bytes on which an instance
	// of this type should be aligned in Mojo serialization.
	SerializationAlignment() uint32
}

/////////////////////////////////////////////////////////////
// type UserDefinedTypeBase
/////////////////////////////////////////////////////////////

// This struct is embedded in each of MojomStruct, MojomInterface
// MojomEnum and MojomUnion
type UserDefinedTypeBase struct {
	DeclarationData
	thisType UserDefinedType
	typeKey  string

	// Cache the fact that |SetKnownWellFounded| has been invoked.
	knownWellFounded bool
}

// This method is invoked from the constructors for the containing types:
// NewMojomInterface, NewMojomStruct, NewMojomEnum, NewMojomUnion
func (b *UserDefinedTypeBase) Init(declData DeclarationData, thisType UserDefinedType) {
	b.thisType = thisType
	b.DeclarationData = declData
	b.DeclarationData.declaredObject = thisType
}

func (b *UserDefinedTypeBase) TypeKind() TypeKind {
	return TypeKindUserDefined
}

func (b *UserDefinedTypeBase) KindString() string {
	return b.thisType.Kind().String()
}

// Generates the fully-qualified name and the type Key and registers the
// type in the given |scope| and also with the associated MojomDescriptor.
//
// This method is invoked when a UserDefinedType is added to its container,
// which may be either a file or a different UserDefinedType.
func (b *UserDefinedTypeBase) RegisterInScope(scope *Scope) DuplicateNameError {
	if scope == nil {
		panic("scope is nil")
	}

	// Set the scope on b before invoking RegisterType().
	b.scope = scope
	if err := scope.RegisterType(b.thisType); err != nil {
		return err
	}

	b.fullyQualifiedName = buildDottedName(scope.fullyQualifiedName, b.simpleName)
	b.typeKey = ComputeTypeKey(b.fullyQualifiedName)
	scope.descriptor.TypesByKey[b.typeKey] = b.thisType
	return nil
}

func (b UserDefinedTypeBase) String() string {
	attributeString := ""
	if b.attributes != nil {
		attributeString = fmt.Sprintf("%s", b.attributes.List)
	}
	return fmt.Sprintf("(%s)%s%s", b.typeKey, attributeString, b.simpleName)
}

func (b *UserDefinedTypeBase) TypeKey() string {
	return b.typeKey
}

func (b UserDefinedTypeBase) Scope() *Scope {
	return b.scope
}

func (b *UserDefinedTypeBase) CheckWellFounded() error {
	cycleDescription := utils.CheckWellFounded(b)
	if cycleDescription != nil {
		firstIllFoundedType := nodeToUserDefinedType(cycleDescription.First)
		var buffer bytes.Buffer
		fmt.Fprintf(&buffer, "The type %s is unserializable: Every instance of this type would include a cycle.",
			firstIllFoundedType.FullyQualifiedName())
		fmt.Fprintf(&buffer, "\nExample cycle: %s", firstIllFoundedType.SimpleName())
		for _, edge := range cycleDescription.Path {
			fmt.Fprintf(&buffer, ".%s -> %s", edge.Label.(lexer.Token).Text, nodeToUserDefinedType(edge.Target).SimpleName())
		}
		fmt.Fprintf(&buffer, "\n")
		fmt.Fprintf(&buffer, "One way to break this cycle is to make the field %q nullable.", cycleDescription.Path[0].Label.(lexer.Token).Text)
		return fmt.Errorf(UserErrorMessage(
			firstIllFoundedType.Scope().file, cycleDescription.Path[0].Label.(lexer.Token), buffer.String()))
	}
	return nil
}

// *UserDefinedTypeBase implements utils.Node for the sake of |CheckWellFounded|.
func (b *UserDefinedTypeBase) KnownWellFounded() bool {
	return b.knownWellFounded
}

// *UserDefinedTypeBase implements utils.Node for the sake of |CheckWellFounded|.
func (b *UserDefinedTypeBase) SetKnownWellFounded() {
	b.knownWellFounded = true
}

// *UserDefinedTypeBase implements utils.Node for the sake of |CheckWellFounded|.
func (b *UserDefinedTypeBase) Name() string {
	return b.FullyQualifiedName()
}

// *UserDefinedTypeBase implements utils.Node for the sake of |CheckWellFounded|.
func (b *UserDefinedTypeBase) IsSquare() bool {
	// A node in the type graph is square if and only if the type is a union.
	return b.thisType.Kind() == UserDefinedTypeKindUnion
}

// *UserDefinedTypeBase implements utils.Node for the sake of |CheckWellFounded|.
func (b *UserDefinedTypeBase) OutEdges() []utils.OutEdge {
	// The only types we need to model as nodes are structs and unions.
	// We model some of their fields as children: fields whose type is a non-nullable
	// pointer or a fixed-length array to another struct or union.
	outEdges := make([]utils.OutEdge, 0)
	switch udt := b.thisType.(type) {
	case *MojomEnum, *MojomInterface:
		return nil
	case *MojomStruct:
		for _, field := range udt.FieldsInLexicalOrder {
			childType := field.FieldType.NonAvoidableUserType()
			if childType != nil {
				outEdges = append(outEdges, utils.OutEdge{field.nameToken, getTypeBase(childType)})
			}
		}
	case *MojomUnion:
		// Unlike with structs, when modelling a union as a node in the type graph, it is important
		// that we do not discard the information that there is at least one child that is a known leaf node.
		nameOfLeafChild := ""
		leafEdgeNameToken := lexer.Token{}
		for _, field := range udt.FieldsInLexicalOrder {
			childType := field.FieldType.NonAvoidableUserType()
			if childType != nil {
				outEdges = append(outEdges, utils.OutEdge{field.nameToken, getTypeBase(childType)})
			} else if len(nameOfLeafChild) == 0 {
				nameOfLeafChild = field.FieldType.TypeName()
				leafEdgeNameToken = field.nameToken
			}
		}
		if len(outEdges) > 0 && len(nameOfLeafChild) > 0 {
			// We have added at least one out-edge to a node that may or may not be a leaf node.
			// We know that there is at least one child which is known to be a leaf but for which
			// we have supressed adding an out-edge. We now add an
			// out edge to a synthetic leaf node so that the information that there was at least
			// one is not lost. This is important because unions are modelled as square nodes
			// and so the existence of at least one out edge to a leaf node is critical.
			outEdges = append(outEdges, utils.OutEdge{leafEdgeNameToken, utils.NewLeafNode(nameOfLeafChild)})
		}
	default:
		panic(fmt.Sprintf("unexpected kind %T", udt))
	}
	return outEdges
}

// getTypeBase returns the *UserDefinedTypeBase contained in the given UserDefinedType
func getTypeBase(udt UserDefinedType) *UserDefinedTypeBase {
	switch udt := udt.(type) {
	case *MojomStruct:
		return &udt.UserDefinedTypeBase
	case *MojomUnion:
		return &udt.UserDefinedTypeBase
	case *MojomEnum:
		return &udt.UserDefinedTypeBase
	case *MojomInterface:
		return &udt.UserDefinedTypeBase
	default:
		panic(fmt.Sprintf("Unrecognized user defined type %T", udt))
	}
}

// nodeToUserDefinedType assumes that the type of |node| is *UserDefinedTypeBase
// and returns the associated UserDefinedType
func nodeToUserDefinedType(node utils.Node) UserDefinedType {
	return node.(*UserDefinedTypeBase).thisType
}

/////////////////////////////////////////////////////////////
// type DeclaredObjectsContainerBase
/////////////////////////////////////////////////////////////

// DeclaredObjectsContainerBase holds a list of DeclaredObjects in order of
// occurrence in the source.
// It includes all declared objects (for example methods and fields) not just
// enums and constants.
type DeclaredObjectsContainerBase struct {
	DeclaredObjects []DeclaredObject
	// The token of the right brace that terminates the body of the declared
	// objects container.
	closingBraceToken *lexer.Token
}

func (c DeclaredObjectsContainerBase) GetDeclaredObjects() []DeclaredObject {
	return c.DeclaredObjects
}

func (c *DeclaredObjectsContainerBase) ClosingBraceToken() *lexer.Token {
	return c.closingBraceToken
}

func (c *DeclaredObjectsContainerBase) SetClosingBraceToken(closingBraceToken *lexer.Token) {
	c.closingBraceToken = closingBraceToken
}

type DeclaredObjectsContainer interface {
	GetDeclaredObjects() []DeclaredObject
}

/////////////////////////////////////////////////////////////
// type NestedDeclarations
/////////////////////////////////////////////////////////////

// Some user-defined types, namely interfaces and structs, may act as
// namespaced scopes for declarations of constants and enums.
type NestedDeclarations struct {
	DeclaredObjectsContainerBase
	containedScope *Scope
	Enums          []*MojomEnum
	Constants      []*UserDefinedConstant
}

// Adds an enum to the type associated with this NestedDeclarations,
// which must be an interface or struct.
func (c *NestedDeclarations) AddEnum(mojomEnum *MojomEnum) DuplicateNameError {
	c.DeclaredObjects = append(c.DeclaredObjects, mojomEnum)
	c.Enums = append(c.Enums, mojomEnum)
	return mojomEnum.RegisterInScope(c.containedScope)
}

// Adds a declared constant to this type, which must be an interface or struct.
func (c *NestedDeclarations) AddConstant(declaredConst *UserDefinedConstant) DuplicateNameError {
	c.DeclaredObjects = append(c.DeclaredObjects, declaredConst)
	c.Constants = append(c.Constants, declaredConst)
	if declaredConst == nil {
		panic("declaredConst is nil")
	}
	return declaredConst.RegisterInScope(c.containedScope)
}

/////////////////////////////////////////////////////////////
// Structs
/////////////////////////////////////////////////////////////

type StructType int

const (
	// A  regular struct.
	StructTypeRegular StructType = iota

	// A synthetic method request parameter struct. In this case the name
	/// of the struct will be the name of the method.
	StructTypeSyntheticRequest

	// A synthetic method response parameter struct. In this case the name
	// of the struct will be the name of the method.
	StructTypeSyntheticResponse
)

type MojomStruct struct {
	UserDefinedTypeBase
	NestedDeclarations

	structType StructType

	fieldsByName         map[string]*StructField
	FieldsInLexicalOrder []*StructField

	// This is computed by ComputeFieldOrdinals().
	fieldsInOrdinalOrder []*StructField

	// This is computed in computeVersionInfo which is invoked by ComputeFinalData().
	versionInfo []StructVersion

	// Used to form an error message in case of a duplicate field name.
	userFacingName string
}

func NewMojomStruct(declData DeclarationData) *MojomStruct {
	mojomStruct := new(MojomStruct)
	mojomStruct.fieldsByName = make(map[string]*StructField)
	mojomStruct.FieldsInLexicalOrder = make([]*StructField, 0)
	mojomStruct.Init(declData, mojomStruct)
	mojomStruct.userFacingName = mojomStruct.simpleName
	return mojomStruct
}

const requestSuffix = "-request"
const responseSuffix = "-response"

func NewSyntheticRequestStruct(methodName string, nameToken lexer.Token, owningFile *MojomFile) *MojomStruct {
	mojomStruct := NewMojomStruct(DeclData(methodName+requestSuffix, owningFile, nameToken, nil))
	mojomStruct.structType = StructTypeSyntheticRequest
	mojomStruct.userFacingName = methodName
	return mojomStruct
}

func NewSyntheticResponseStruct(methodName string, nameToken lexer.Token, owningFile *MojomFile) *MojomStruct {
	mojomStruct := NewMojomStruct(DeclData(methodName+responseSuffix, owningFile, nameToken, nil))
	mojomStruct.structType = StructTypeSyntheticResponse
	mojomStruct.userFacingName = methodName
	return mojomStruct
}

func (s *MojomStruct) InitAsScope(parentScope *Scope) *Scope {
	s.containedScope = NewLexicalScope(ScopeStruct, parentScope, s.simpleName, parentScope.file, s)
	return s.containedScope
}

type DuplicateMemberNameError struct {
	DuplicateNameErrorBase
	duplicateObjectType, containerType, containerName string
}

func (e *DuplicateMemberNameError) Error() string {
	return UserErrorMessage(e.owningFile, e.nameToken,
		fmt.Sprintf("Duplicate definition of '%s'. "+
			"There is already a %s with that name in %s %s.",
			e.nameToken.Text, e.duplicateObjectType, e.containerType, e.containerName))
}

func (s *MojomStruct) AddField(field *StructField) DuplicateNameError {
	if field == nil {
		panic("field is nil")
	}
	if _, ok := s.fieldsByName[field.simpleName]; ok {
		var duplicateObjectType, containerType string
		switch s.structType {
		case StructTypeRegular:
			duplicateObjectType = "field"
			containerType = "struct"
		case StructTypeSyntheticRequest:
			duplicateObjectType = "request parameter"
			containerType = "method"
		case StructTypeSyntheticResponse:
			duplicateObjectType = "response parameter"
			containerType = "method"
		}
		return &DuplicateMemberNameError{
			DuplicateNameErrorBase{nameToken: field.NameToken(), owningFile: field.OwningFile()},
			duplicateObjectType, containerType, s.userFacingName}
	}
	if s.structType == StructTypeRegular {
		// Only a regular struct has a contained scope.
		if err := field.RegisterInScope(s.containedScope); err != nil {
			return err
		}
	}
	s.fieldsByName[field.simpleName] = field
	field.lexicalPosition = int32(len(s.FieldsInLexicalOrder))
	s.FieldsInLexicalOrder = append(s.FieldsInLexicalOrder, field)
	s.DeclaredObjects = append(s.DeclaredObjects, field)
	return nil
}

func (s *MojomStruct) FieldsInOrdinalOrder() []*StructField {
	if s.fieldsInOrdinalOrder == nil {
		panic("The method ComputeFieldOrdinals() must be invoked first.")
	}
	return s.fieldsInOrdinalOrder
}

func (s *MojomStruct) VersionInfo() []StructVersion {
	if s.versionInfo == nil {
		panic("The method computeVersionInfo() must be invoked first.")
	}
	return s.versionInfo
}

func (*MojomStruct) Kind() UserDefinedTypeKind {
	return UserDefinedTypeKindStruct
}

func (*MojomStruct) SerializationSize() uint32 {
	return 8
}

func (*MojomStruct) SerializationAlignment() uint32 {
	return 8
}

func (s *MojomStruct) StructType() StructType {
	return s.structType
}

func (MojomStruct) IsAssignmentCompatibleWith(value LiteralValue) bool {
	return value.IsDefault()
}

var ErrOrdinalRange = errors.New("ordinal value out of range")
var ErrOrdinalDuplicate = errors.New("duplicate ordinal value")

type StructFieldOrdinalError struct {
	Ord           int64        // The attemted ordinal
	StructName    string       // The name of the struct in which the problem occurs.
	Field         *StructField // The field with the attempted ordinal
	ExistingField *StructField // Used if Err == ErrOrdinalDuplicate
	Err           error        // the type of error (ErrOrdinalRange, ErrOrdinalDuplicate)
}

// StructFieldOrdinalError implements error.
func (e *StructFieldOrdinalError) Error() string {
	var message string
	switch e.Err {
	case ErrOrdinalRange:
		message = fmt.Sprintf("Invalid ordinal for field %s: %d. "+
			"A struct field ordinal must be a non-negative integer value "+
			"less than the number of fields in the struct.",
			e.Field.SimpleName(), e.Ord)
	case ErrOrdinalDuplicate:
		message = fmt.Sprintf("Invalid ordinal for field %s: %d. "+
			"There is already a field in struct %s with that ordinal: %s.",
			e.Field.SimpleName(), e.Ord, e.StructName,
			e.ExistingField.SimpleName())
	default:
		panic(fmt.Sprintf("Unrecognized type of StructFieldOrdinalError %v", e.Err))
	}
	return UserErrorMessage(e.Field.OwningFile(), e.Field.NameToken(), message)
}

// This should be invoked some time after all of the fields have been added
// to the struct.
func (s *MojomStruct) ComputeFieldOrdinals() error {
	numFields := uint32(len(s.FieldsInLexicalOrder))
	s.fieldsInOrdinalOrder = make([]*StructField, numFields)
	nextOrdinal := uint32(0)
	for _, field := range s.FieldsInLexicalOrder {
		fieldOrdinal := nextOrdinal
		if field.declaredOrdinal >= 0 {
			if field.declaredOrdinal >= math.MaxUint32 {
				return &StructFieldOrdinalError{Ord: field.declaredOrdinal,
					StructName: s.SimpleName(), Field: field,
					Err: ErrOrdinalRange}
			}
			fieldOrdinal = uint32(field.declaredOrdinal)
		}
		if fieldOrdinal >= numFields {
			return &StructFieldOrdinalError{Ord: int64(fieldOrdinal),
				StructName: s.SimpleName(), Field: field,
				Err: ErrOrdinalRange}
		}
		if existingField := s.fieldsInOrdinalOrder[fieldOrdinal]; existingField != nil {
			return &StructFieldOrdinalError{Ord: int64(fieldOrdinal),
				StructName: s.SimpleName(), Field: field,
				ExistingField: existingField, Err: ErrOrdinalDuplicate}
		}
		s.fieldsInOrdinalOrder[fieldOrdinal] = field
		nextOrdinal = fieldOrdinal + 1
	}
	return nil
}

func (m MojomStruct) String() string {
	s := fmt.Sprintf("\n---------struct--------------\n")
	s += fmt.Sprintf("%s\n", m.UserDefinedTypeBase)
	s += "     Fields\n"
	s += "     ------\n"
	for _, field := range m.FieldsInLexicalOrder {
		s += fmt.Sprintf("     %s\n", field)
	}
	s += "     Enums\n"
	s += "     ------\n"
	for _, enum := range m.Enums {
		s += fmt.Sprintf(enum.toString(5))
	}
	s += "     Constants\n"
	s += "     --------"
	for _, constant := range m.Constants {
		s += fmt.Sprintf(constant.toString(5))
	}
	return s
}

// A debug string representing this struct in the case that this struct
// is being used to represent the parameters to a method.
func (s MojomStruct) ParameterString() string {
	str := ""
	for i, f := range s.FieldsInLexicalOrder {
		if i > 0 {
			str += ", "
		}
		attributesString := ""
		if f.attributes != nil {
			attributesString = fmt.Sprintf("%s", f.attributes)
		}
		ordinalString := ""
		if f.declaredOrdinal >= 0 {
			ordinalString = fmt.Sprintf("@%d", f.declaredOrdinal)
		}
		str += fmt.Sprintf("%s%s %s%s", attributesString, f.FieldType,
			f.simpleName, ordinalString)
	}
	return str
}

type StructField struct {
	VersionedDeclarationData

	FieldType    TypeRef
	DefaultValue ValueRef

	// Computed Data. The values are computed and set in
	// See mojom_types.mojom for the meanings.

	// A valid offset is a uint32. We use -1 to indicate unset.
	offset int64

	// A valid |bit| value is a uint8. We use -1 to indicate unset.
	bit int16
}

func NewStructField(declData DeclarationData, fieldType TypeRef, defaultValue ValueRef) *StructField {
	field := StructField{FieldType: fieldType, DefaultValue: defaultValue}
	declData.declaredObject = &field
	field.DeclarationData = declData
	field.offset = -1
	field.bit = -1
	field.VersionedDeclarationData.init()
	return &field
}

func (f *StructField) ValidateDefaultValue() (ok bool) {
	if f.DefaultValue == nil {
		return true
	}
	if literalValue, ok := f.DefaultValue.(LiteralValue); ok {
		// The default value is a literal value. It is only this case we have to
		// handle now because if the default value were instead a reference then
		// it was already marked with the field type as it's assignee type and
		// it will be validated after resolution.
		assignment := LiteralAssignment{assignedValue: literalValue,
			variableName: f.SimpleName(), kind: LiteralAssignmentKindDefaultStructField}
		return f.FieldType.MarkTypeCompatible(assignment)
	}
	return true
}

func (f *StructField) RegisterInScope(scope *Scope) DuplicateNameError {
	// Set the scope on f before invoking RegisterValue().
	f.scope = scope
	if err := scope.RegisterStructField(f); err != nil {
		return err
	}

	if scope.kind != ScopeStruct {
		panic("A struct field  may only be registered within the scope of a struct.")
	}
	// Note that we give a struct field a fully-qualified name only for the purpose
	// of being able to refer to it in error messages. A struct field is not
	// a stand-alone entity: It may not be referred to in a .mojom file and it is
	// not given a type key.
	f.fullyQualifiedName = buildDottedName(scope.fullyQualifiedName, f.simpleName)
	return nil
}

func (f *StructField) Scope() *Scope {
	return f.scope
}

func (f *StructField) KindString() string {
	return "field"
}

func (f *StructField) Offset() uint32 {
	if f.offset < 0 {
		panic("The method ComputeFieldOffsets() must first be invoked for the containing struct.")
	}
	return uint32(f.offset)
}

func (f *StructField) Bit() uint8 {
	if f.bit < 0 {
		panic("The method ComputeFieldOffsets() must first be invoked for the containing struct.")
	}
	return uint8(f.bit)
}

func (f StructField) String() string {
	attributeString := ""
	if f.attributes != nil {
		attributeString = f.attributes.String()
	}
	defaultValueString := ""
	if f.DefaultValue != nil {
		defaultValueString = fmt.Sprintf(" = %s", f.DefaultValue)
	}
	ordinalString := ""
	if f.declaredOrdinal >= 0 {
		ordinalString = fmt.Sprintf("@%d", f.declaredOrdinal)
	}
	return fmt.Sprintf("%s%s %s%s%s", attributeString, f.FieldType, f.simpleName, ordinalString, defaultValueString)
}

// See StructVersion in mojom_types.mojom for a description of the fields.
type StructVersion struct {
	VersionNumber uint32
	NumFields     uint32
	NumBytes      uint32
}

/////////////////////////////////////////////////////////////
// Interfaces and Methods
/////////////////////////////////////////////////////////////

type MojomInterface struct {
	UserDefinedTypeBase
	NestedDeclarations

	MethodsByOrdinal map[uint32]*MojomMethod

	methodsByName map[string]*MojomMethod

	methodsByLexicalOrder []*MojomMethod

	// If the declaration of this interface has been annotated with the
	// "ServiceName=" attribute then this field contains the value of that
	// attribute, otherwise this is null.
	ServiceName *string

	// Valid versionNumbers are uint32s. We use -1 to mean unset.
	// The versionNumber of a MojomInterface is the maximum value of
	// of the MinVersion values of all of its methods and parameters.
	// This value is computed in computeInterfaceVersion() and accessed
	// via Version(). Initially it is -1.
	versionNumber int64

	// methodsInOrdinalOrder is computed by ComputeMethodOrdinals()
	// and accessed via MethodsInOrdinalOrder(). Initially it is nil.
	methodsInOrdinalOrder []*MojomMethod
}

func NewMojomInterface(declData DeclarationData) *MojomInterface {
	mojomInterface := new(MojomInterface)
	mojomInterface.MethodsByOrdinal = make(map[uint32]*MojomMethod)
	mojomInterface.methodsByName = make(map[string]*MojomMethod)
	mojomInterface.Init(declData, mojomInterface)
	// Search for an attribute named "ServiceName" with a string value.
	// If that is found take the value as |ServiceName|.
	if declData.attributes != nil && declData.attributes.List != nil {
		for _, attribute := range declData.attributes.List {
			if attribute.Key == "ServiceName" {
				if valueString, ok := attribute.Value.Value().(string); ok {
					mojomInterface.ServiceName = &valueString
					break
				}
			}
		}
	}
	mojomInterface.versionNumber = -1
	return mojomInterface
}

func (i *MojomInterface) InitAsScope(parentScope *Scope) *Scope {
	i.containedScope = NewLexicalScope(ScopeInterface, parentScope,
		i.simpleName, parentScope.file, i)
	return i.containedScope
}

func (i *MojomInterface) AddMethod(method *MojomMethod) DuplicateNameError {
	if err := method.RegisterInScope(i.containedScope); err != nil {
		return err
	}
	i.methodsByName[method.simpleName] = method
	method.lexicalPosition = int32(len(i.methodsByLexicalOrder))
	i.methodsByLexicalOrder = append(i.methodsByLexicalOrder, method)
	i.DeclaredObjects = append(i.DeclaredObjects, method)
	return nil
}

func (MojomInterface) Kind() UserDefinedTypeKind {
	return UserDefinedTypeKindInterface
}

func (*MojomInterface) SerializationSize() uint32 {
	return 8
}

func (*MojomInterface) SerializationAlignment() uint32 {
	return 4
}

func (MojomInterface) IsAssignmentCompatibleWith(value LiteralValue) bool {
	return false
}

func (intrfc *MojomInterface) Version() uint32 {
	if intrfc.versionNumber < 0 {
		panic("The method ComputeFinalData() must first be invoked.")
	}
	return uint32(intrfc.versionNumber)
}

type MethodOrdinalError struct {
	Ord            int64        // The attemted ordinal
	InterfaceName  string       // The name of the interface in which the problem occurs.
	Method         *MojomMethod // The method with the attempted ordinal
	ExistingMethod *MojomMethod // Used if Err == ErrOrdinalDuplicate
	Err            error        // the type of error (ErrOrdinalRange, ErrOrdinalDuplicate)
}

// MethodOrdinalError implements error.
func (e *MethodOrdinalError) Error() string {
	var message string
	switch e.Err {
	case ErrOrdinalRange:
		message = fmt.Sprintf("Invalid method ordinal for method %s: %d. "+
			"A method ordinal must be between 0 and 4,294,967,294.",
			e.Method.SimpleName(), e.Ord)
	case ErrOrdinalDuplicate:
		message = fmt.Sprintf("Invalid method ordinal for method %s: %d. "+
			"There is already a method in interface %s with that ordinal: %s.",
			e.Method.SimpleName(), e.Ord, e.InterfaceName,
			e.ExistingMethod.SimpleName())
	default:
		panic(fmt.Sprintf("Unrecognized type of MethodOrdinalError %v", e.Err))
	}
	return UserErrorMessage(e.Method.OwningFile(), e.Method.NameToken(), message)
}

func (intrfc *MojomInterface) ComputeMethodOrdinals() error {
	methodOrdinals := make([]uint32, len(intrfc.methodsByLexicalOrder))
	nextOrdinal := uint32(0)
	for i, method := range intrfc.methodsByLexicalOrder {
		if method.declaredOrdinal < 0 {
			method.Ordinal = nextOrdinal
		} else {
			if method.declaredOrdinal >= math.MaxUint32 {
				return &MethodOrdinalError{Ord: method.declaredOrdinal,
					InterfaceName: intrfc.SimpleName(), Method: method,
					Err: ErrOrdinalRange}
			}
			method.Ordinal = uint32(method.declaredOrdinal)
		}
		if existingMethod, ok := intrfc.MethodsByOrdinal[method.Ordinal]; ok {
			return &MethodOrdinalError{Ord: int64(method.Ordinal),
				InterfaceName: intrfc.SimpleName(), Method: method,
				ExistingMethod: existingMethod, Err: ErrOrdinalDuplicate}
		}
		methodOrdinals[i] = method.Ordinal
		intrfc.MethodsByOrdinal[method.Ordinal] = method
		nextOrdinal = method.Ordinal + 1
	}
	sort.Sort(utils.UInt32Slice(methodOrdinals))
	intrfc.methodsInOrdinalOrder = make([]*MojomMethod, len(methodOrdinals))
	for i, ordinal := range methodOrdinals {
		intrfc.methodsInOrdinalOrder[i] = intrfc.MethodsByOrdinal[ordinal]
	}
	return nil
}

func (intrfc *MojomInterface) MethodsInOrdinalOrder() []*MojomMethod {
	if intrfc.methodsInOrdinalOrder == nil {
		panic("ComputeMethodOrdinals() must be invoked first.")
	}
	return intrfc.methodsInOrdinalOrder
}

func (m *MojomInterface) String() string {
	if m == nil {
		return "nil"
	}
	s := fmt.Sprintf("\n---------interface--------------\n")
	s += fmt.Sprintf("%s\n", m.UserDefinedTypeBase)
	s += "     Methods\n"
	s += "     -------\n"
	for _, method := range m.methodsByName {
		s += fmt.Sprintf("     %s\n", method)
	}
	return s
}

type MojomMethod struct {
	VersionedDeclarationData

	// The ordinal field differs from the declaredOrdinal field
	// in DeclarationData because every method eventually gets
	// assigned an ordinal whereas the declaredOrdinal is only set
	// if the user explicitly sets it in the .mojom file.
	Ordinal uint32

	Parameters *MojomStruct

	ResponseParameters *MojomStruct
}

func NewMojomMethod(declData DeclarationData, params, responseParams *MojomStruct) *MojomMethod {
	mojomMethod := new(MojomMethod)
	declData.declaredObject = mojomMethod
	mojomMethod.DeclarationData = declData
	mojomMethod.Parameters = params
	mojomMethod.ResponseParameters = responseParams
	mojomMethod.VersionedDeclarationData.init()
	return mojomMethod
}

func (m *MojomMethod) String() string {
	parameterString := m.Parameters.ParameterString()
	responseString := ""
	if m.ResponseParameters != nil {
		responseString = fmt.Sprintf(" => (%s)", m.ResponseParameters.ParameterString())
	}
	return fmt.Sprintf("%s(%s)%s", m.simpleName, parameterString, responseString)
}

func (m *MojomMethod) RegisterInScope(scope *Scope) DuplicateNameError {
	// Set the scope on m before invoking RegisterValue().
	m.scope = scope
	if err := scope.RegisterMethod(m); err != nil {
		return err
	}

	if scope.kind != ScopeInterface {
		panic("A method  may only be registered within the scope of an interface.")
	}
	m.fullyQualifiedName = buildDottedName(scope.fullyQualifiedName, m.simpleName)
	return nil
}

func (m *MojomMethod) Scope() *Scope {
	return m.scope
}

func (m *MojomMethod) KindString() string {
	return "method"
}

/////////////////////////////////////////////////////////////
// Unions
/////////////////////////////////////////////////////////////
type MojomUnion struct {
	UserDefinedTypeBase
	DeclaredObjectsContainerBase

	fieldsByName         map[string]*UnionField
	FieldsInLexicalOrder []*UnionField
	fieldsInTagOrder     []*UnionField
}

func NewMojomUnion(declData DeclarationData) *MojomUnion {
	mojomUnion := new(MojomUnion)
	mojomUnion.fieldsByName = make(map[string]*UnionField)
	mojomUnion.FieldsInLexicalOrder = make([]*UnionField, 0)
	mojomUnion.Init(declData, mojomUnion)
	return mojomUnion
}

// Adds a UnionField to this Union
func (u *MojomUnion) AddField(declData DeclarationData, FieldType TypeRef) DuplicateNameError {
	field := UnionField{FieldType: FieldType}
	declData.declaredObject = &field
	field.DeclarationData = declData
	if _, ok := u.fieldsByName[field.simpleName]; ok {
		return &DuplicateMemberNameError{
			DuplicateNameErrorBase{nameToken: field.NameToken(), owningFile: field.OwningFile()},
			"field", "union", u.simpleName}
	}
	u.fieldsByName[field.simpleName] = &field
	field.lexicalPosition = int32(len(u.FieldsInLexicalOrder))
	u.FieldsInLexicalOrder = append(u.FieldsInLexicalOrder, &field)
	u.DeclaredObjects = append(u.DeclaredObjects, &field)
	return nil
}

func (u *MojomUnion) FieldsInTagOrder() []*UnionField {
	if u.fieldsInTagOrder == nil {
		panic("The method ComputeFieldTags() must be invoked first.")
	}
	return u.fieldsInTagOrder
}

type UnionFieldTagOrdinalError struct {
	Ord           int64       // The attemted ordinal
	UnionName     string      // The name of the union in which the problem occurs.
	Field         *UnionField // The field with the attempted ordinal
	ExistingField *UnionField // Used if Err == ErrOrdinalDuplicate
	Err           error       // the type of error (ErrOrdinalRange, ErrOrdinalDuplicate)
}

// UnionFieldTagOrdinalError implements error.
func (e *UnionFieldTagOrdinalError) Error() string {
	var message string
	switch e.Err {
	case ErrOrdinalRange:
		message = fmt.Sprintf("Invalid tag for field %s: %d. "+
			"A union field tag must be between 0 and 4,294,967,294.",
			e.Field.SimpleName(), e.Ord)
	case ErrOrdinalDuplicate:
		message = fmt.Sprintf("Invalid tag for field %s: %d. "+
			"There is already a field in union %s with that tag: %s.",
			e.Field.SimpleName(), e.Ord, e.UnionName,
			e.ExistingField.SimpleName())
	default:
		panic(fmt.Sprintf("Unrecognized type of UnionFieldTagOrdinalError %v", e.Err))
	}
	return UserErrorMessage(e.Field.OwningFile(), e.Field.NameToken(), message)
}

// This should be invoked some time after all of the fields have been added
// to the union.
func (u *MojomUnion) ComputeFieldTags() error {
	numFields := uint32(len(u.FieldsInLexicalOrder))
	fieldsByTag := make(map[uint32]*UnionField)
	allTags := make([]uint32, numFields)
	previousTag := uint32(math.MaxUint32) // initialize to (uint32)(-1)
	for i, field := range u.FieldsInLexicalOrder {
		var fieldTag uint32
		if field.declaredOrdinal >= 0 {
			if field.declaredOrdinal >= math.MaxUint32 {
				return &UnionFieldTagOrdinalError{Ord: field.declaredOrdinal,
					UnionName: u.SimpleName(), Field: field,
					Err: ErrOrdinalRange}
			}
			fieldTag = uint32(field.declaredOrdinal)
		} else {
			if previousTag >= (math.MaxUint32-1) && i > 0 {
				return &UnionFieldTagOrdinalError{Ord: int64(previousTag) + 1,
					UnionName: u.SimpleName(), Field: field,
					Err: ErrOrdinalRange}
			}
			fieldTag = previousTag + 1
		}
		if existingField := fieldsByTag[fieldTag]; existingField != nil {
			return &UnionFieldTagOrdinalError{Ord: int64(fieldTag),
				UnionName: u.SimpleName(), Field: field,
				ExistingField: existingField, Err: ErrOrdinalDuplicate}
		}
		fieldsByTag[fieldTag] = field
		allTags[i] = fieldTag
		field.Tag = fieldTag
		previousTag = fieldTag
	}
	sort.Sort(utils.UInt32Slice(allTags))
	u.fieldsInTagOrder = make([]*UnionField, numFields)
	for i, tag := range allTags {
		u.fieldsInTagOrder[i] = fieldsByTag[tag]
	}
	return nil
}

func (MojomUnion) Kind() UserDefinedTypeKind {
	return UserDefinedTypeKindUnion
}

func (MojomUnion) SerializationSize() uint32 {
	return 16
}

func (MojomUnion) SerializationAlignment() uint32 {
	return 8
}

func (MojomUnion) IsAssignmentCompatibleWith(value LiteralValue) bool {
	return false
}

type UnionField struct {
	DeclarationData

	FieldType TypeRef
	Tag       uint32
}

func (f *UnionField) RegisterInScope(scope *Scope) DuplicateNameError {
	// We currently have no reason to register a UnionField in a scope.
	panic("Not implemented.")
}

func (f *UnionField) Scope() *Scope {
	return f.scope
}

func (f *UnionField) KindString() string {
	return "union field"
}

/////////////////////////////////////////////////////////////
// Enums
/////////////////////////////////////////////////////////////
type MojomEnum struct {
	UserDefinedTypeBase
	DeclaredObjectsContainerBase

	Values         []*EnumValue
	scopeForValues *Scope
}

func NewMojomEnum(declData DeclarationData) *MojomEnum {
	mojomEnum := new(MojomEnum)
	mojomEnum.Values = make([]*EnumValue, 0)
	mojomEnum.Init(declData, mojomEnum)
	return mojomEnum
}

// A MojoEnum is a ConcreteType
func (MojomEnum) ConcreteTypeKind() TypeKind {
	return TypeKindUserDefined
}

func (e MojomEnum) IsAssignmentCompatibleWith(value LiteralValue) bool {
	return false
}

func (MojomEnum) Kind() UserDefinedTypeKind {
	return UserDefinedTypeKindEnum
}

func (MojomEnum) SerializationSize() uint32 {
	return 4
}

func (MojomEnum) SerializationAlignment() uint32 {
	return 4
}

func (e *MojomEnum) InitAsScope(parentScope *Scope) *Scope {
	e.scopeForValues = NewLexicalScope(ScopeEnum, parentScope,
		e.simpleName, parentScope.file, e)
	return e.scopeForValues
}

// Adds an EnumValue to this enum
func (e *MojomEnum) AddEnumValue(declData DeclarationData, valueRef ValueRef) DuplicateNameError {
	enumValue := new(EnumValue)
	enumValue.Init(declData, UserDefinedValueKindEnumValue, enumValue, valueRef)
	enumValue.valueIndex = uint32(len(e.Values))
	e.Values = append(e.Values, enumValue)
	e.DeclaredObjects = append(e.DeclaredObjects, enumValue)
	enumValue.enumType = e
	if e.scopeForValues == nil {
		return nil
	}
	return enumValue.RegisterInScope(e.scopeForValues)
}

func (e *MojomEnum) String() string {
	s := fmt.Sprintf("\n---------enum--------------\n")
	s += e.toString(0)
	return s
}

func (e *MojomEnum) toString(indentLevel int) string {
	indent := strings.Repeat(" ", indentLevel)
	s := fmt.Sprintf("%s%s\n", indent, e.UserDefinedTypeBase)
	s += indent + "     Values\n"
	s += indent + "     ------\n"
	for _, value := range e.Values {
		s += fmt.Sprintf(indent+"     %s", value)
	}
	return s
}

// An EnumValue is a ConcreteValue and a UserDefinedValue.
type EnumValue struct {
	UserDefinedValueBase

	enumType *MojomEnum
	// The 0-based index of this EnumValue in the |values| slice of |enumType|.
	valueIndex uint32

	// After all values in the MojomDescriptor have been resolved,
	// MojomDescriptor.ComputeEnumValueIntegers() should be invoked. This
	// computes |ComputedIntValue| for all EnumValues. The field
	// IntValueComputed is set to true to indicate the value has been
	// computed
	ComputedIntValue int32
	IntValueComputed bool
}

func (ev *EnumValue) EnumType() *MojomEnum {
	return ev.enumType
}

func (ev *EnumValue) ValueIndex() uint32 {
	return ev.valueIndex
}

// EnumValue implements ConcreteValue
func (ev *EnumValue) ValueType() ConcreteType {
	return ev.enumType
}
func (ev *EnumValue) Value() interface{} {
	return *ev
}

func (ev *EnumValue) String() string {
	return fmt.Sprintf("%s\n", ev.UserDefinedValueBase)
}

/////////////////////////////////////////////////////////////
// Values
/////////////////////////////////////////////////////////////

// User-Defined Value Kinds
type UserDefinedValueKind int

const (
	UserDefinedValueKindEnumValue UserDefinedValueKind = iota
	UserDefinedValueKindDeclaredConst
	UserDefinedValueKindBuiltInConst
)

func (k UserDefinedValueKind) String() string {
	switch k {
	case UserDefinedValueKindEnumValue:
		return "enum value"
	case UserDefinedValueKindDeclaredConst:
		return "const"
	case UserDefinedValueKindBuiltInConst:
		return "built-in constant"
	default:
		panic(fmt.Sprintf("Unknown UserDefinedValueKind: %d", k))
	}
}

// A UserDefinedValue is a UserDefinedConstant an EnumValue, or a
// BuiltInConstantValue
type UserDefinedValue interface {
	DeclaredObject
	Kind() UserDefinedValueKind
	ValueKey() string
}

type UserDefinedValueBase struct {
	DeclarationData
	thisValue UserDefinedValue
	kind      UserDefinedValueKind
	valueKey  string
	// This is the value specified in the right-hand-side of the value
	// declaration. For an enum value it will be an integer literal. For
	// a constant declartion it may be a literal or it may be a reference.
	valueRef ValueRef
}

// This method is invoked from the constructors for the containing values:
// NewUserDefinedConstant and AddEnumValue
func (b *UserDefinedValueBase) Init(declData DeclarationData, kind UserDefinedValueKind,
	thisValue UserDefinedValue, valueRef ValueRef) {
	declData.declaredObject = thisValue
	b.DeclarationData = declData
	b.thisValue = thisValue
	b.kind = kind
	b.valueRef = valueRef
}

func (v *UserDefinedValueBase) RegisterInScope(scope *Scope) DuplicateNameError {
	// Set the scope on v before invoking RegisterValue().
	v.scope = scope
	if err := scope.RegisterValue(v.thisValue); err != nil {
		return err
	}

	if v.thisValue.Kind() == UserDefinedValueKindEnumValue {
		if scope.kind != ScopeEnum {
			panic("An enum value may only be registered within the scope of an enum.")
		}
	}

	v.fullyQualifiedName = buildDottedName(scope.fullyQualifiedName, v.simpleName)
	v.valueKey = ComputeTypeKey(v.fullyQualifiedName)
	scope.file.Descriptor.ValuesByKey[v.valueKey] = v.thisValue
	return nil
}

func (b UserDefinedValueBase) String() string {
	attributeString := ""
	if b.attributes != nil {
		attributeString = fmt.Sprintf("%s", b.attributes.List)
	}
	valueRefString := ""
	if b.valueRef != nil {
		valueRefString = fmt.Sprintf(" = %s", b.valueRef)
	}
	return fmt.Sprintf("(%s)%s%s%s", b.valueKey, attributeString, b.simpleName, valueRefString)
}

func (b *UserDefinedValueBase) Kind() UserDefinedValueKind {
	return b.kind
}

func (b UserDefinedValueBase) Scope() *Scope {
	return b.scope
}

func (b UserDefinedValueBase) ValueKey() string {
	return b.valueKey
}

func (b UserDefinedValueBase) ValueRef() ValueRef {
	return b.valueRef
}

func (b *UserDefinedValueBase) KindString() string {
	return b.thisValue.Kind().String()
}

/////////////////////////////////////////////////////////////
//Declared Constants
/////////////////////////////////////////////////////////////

// This represents a Mojom constant declaration.
type UserDefinedConstant struct {
	UserDefinedValueBase

	declaredType TypeRef
}

func NewUserDefinedConstant(declData DeclarationData, declaredType TypeRef, value ValueRef) *UserDefinedConstant {
	constant := new(UserDefinedConstant)
	constant.Init(declData, UserDefinedValueKindDeclaredConst, constant, value)
	constant.declaredType = declaredType
	return constant
}

func (b *UserDefinedConstant) String() string {
	return b.toString(0)
}

func (b *UserDefinedConstant) DeclaredType() TypeRef {
	return b.declaredType
}

func (b *UserDefinedConstant) toString(indentLevel int) string {
	indent := strings.Repeat(" ", indentLevel)
	return fmt.Sprintf("\n%sconst %s %s", indent, b.declaredType, b.UserDefinedValueBase)
}

func (c *UserDefinedConstant) ValidateValue() (ok bool) {
	if literalValue, ok := c.valueRef.(LiteralValue); ok {
		// The value is a literal value. It is only this case we have to
		// handle now because if the  value were instead a reference then
		// it was already marked with the constants declared type as it's
		// assignee type and it will be validated after resolution.
		assignment := LiteralAssignment{assignedValue: literalValue,
			variableName: c.simpleName, kind: LiteralAssignmentKindConstantDeclaration}
		return c.declaredType.MarkTypeCompatible(assignment)
	}
	return true
}

/////////////////////////////////////////////////////////////
// BuiltIn Types and Values
/////////////////////////////////////////////////////////////

// BuiltInConstantType implements ConcreteType.
type BuiltInConstantType int

const (
	BuiltInConstant BuiltInConstantType = 0
)

func (b BuiltInConstantType) String() string {
	if b == BuiltInConstant {
		return "built-in constant"
	} else {
		panic("BuiltInConstant is the only BuiltInConstantType.")
	}
}

func (b BuiltInConstantType) ConcreteTypeKind() TypeKind {
	return TypeKindUserDefined
}

// BuiltInConstantValue implements ConcreteValue and UserDefinedValue.
type BuiltInConstantValue int

const (
	FloatInfinity BuiltInConstantValue = iota
	FloatNegativeInfinity
	FloatNAN
	DoubleInfinity
	DoubleNegativeInfinity
	DoubleNAN
)

func LookupBuiltInConstantValue(identifier string) (val BuiltInConstantValue, ok bool) {
	val, ok = builtInConstantValues[identifier]
	return
}

var allBuiltInConstantValues = []BuiltInConstantValue{FloatInfinity, FloatNegativeInfinity,
	FloatNAN, DoubleInfinity, DoubleNegativeInfinity, DoubleNAN}

var builtInConstantValues map[string]BuiltInConstantValue

func init() {
	builtInConstantValues = make(map[string]BuiltInConstantValue, len(allBuiltInConstantValues))
	for _, b := range allBuiltInConstantValues {
		builtInConstantValues[b.String()] = b
	}
}

func (b BuiltInConstantValue) String() string {
	switch b {
	case FloatInfinity:
		return "float.INFINITY"
	case FloatNegativeInfinity:
		return "float.NEGATIVE_INFINITY"
	case FloatNAN:
		return "float.NAN"
	case DoubleInfinity:
		return "double.INFINITY"
	case DoubleNegativeInfinity:
		return "double.NEGATIVE_INFINITY"
	case DoubleNAN:
		return "double.NAN"
	default:
		panic(fmt.Sprintf("Unknown BuiltInConstantValue %d", b))
	}
}

// From interface DeclaredObject.
func (b BuiltInConstantValue) Attributes() *Attributes {
	panic("BuiltInConstantValue does not have Attributes.")
}

// From interface ConcreteValue
func (BuiltInConstantValue) ValueType() ConcreteType {
	return BuiltInConstant
}

func (b BuiltInConstantValue) Value() interface{} {
	return b
}

// From interface UserDefinedValue

func (b BuiltInConstantValue) SimpleName() string {
	return b.String()
}

func (b BuiltInConstantValue) NameToken() lexer.Token {
	panic("Do not ask for the NameToken of a BuiltInConstantValue.")
}

func (b BuiltInConstantValue) MainToken() *lexer.Token {
	panic("Do not ask for the main token of a BuiltInConstantValue.")
}

func (b BuiltInConstantValue) FullyQualifiedName() string {
	return b.String()
}

func (b BuiltInConstantValue) Kind() UserDefinedValueKind {
	return UserDefinedValueKindBuiltInConst
}

func (b BuiltInConstantValue) KindString() string {
	return "built-in constant"
}

func (b BuiltInConstantValue) Scope() *Scope {
	return nil
}

func (b BuiltInConstantValue) ValueKey() string {
	return "built-in-value:" + b.String()
}

func (b BuiltInConstantValue) RegisterInScope(scope *Scope) DuplicateNameError {
	panic("Do not register a BuiltInConstantValue in a scope.")
}

func (b BuiltInConstantValue) AttachedComments() *AttachedComments {
	panic("BuiltInConstantValue has no attached comments.")
}

func (b BuiltInConstantValue) NewAttachedComments() *AttachedComments {
	panic("BuiltInConstantValue cannot have attached comments.")
}

/////////////////////////////////////////////////////////////
// VersionedDeclarationData
/////////////////////////////////////////////////////////////

// VrsionedDeclarationData is a mixin that should be added to objects that
// support the "MinVersion" attribute.
type VersionedDeclarationData struct {
	DeclarationData

	// A valid min version is a unt32. We use -1 to indicate unset.
	minVersion int64
}

func (v *VersionedDeclarationData) init() {
	v.minVersion = -1
}

const minVersionAttributeName = "MinVersion"

// minVersionAttribute() Attempts to return a uint32 corresponding to the "MinVersion" attribute of this object.
// If found = false then no such attribute could be found and the rest of the return values should be ignored.
// If found = true then |literalValue| is the LiteralValue of the found attribute. In this case:
//    If ok = false then |literalValue| does not contain a uint32 and |value| should be ignored.
//    If ok = true then |literalValue| contains the uint32 value in |value|.
func (v *VersionedDeclarationData) minVersionAttribute() (value uint32, literalValue LiteralValue, found, ok bool) {
	if v.attributes == nil {
		return 0, LiteralValue{}, false, false
	}
	for _, attribute := range v.attributes.List {
		if attribute.Key == minVersionAttributeName {
			value, ok := uint32Value(attribute.Value)
			return value, attribute.Value, true, ok
		}
	}
	return 0, LiteralValue{}, false, false
}

// MinVersion() returns the computed value of MinVersion for this object. This method should only be invoked
// after the method computeVersionInfo() has been invoked on the containing MojomStruct and returned a nil error.
// This method is different than the minVersionAttribute() method in that it does not just return a value
// for objects that have the "MinVersion" attribute explicitly specified. Rather it returns a computed value for
// every object after the "MinVersion" attributes for every object in the container have been checked and validated.
func (v *VersionedDeclarationData) MinVersion() uint32 {
	if v.minVersion < 0 {
		panic("The method computeVersionInfo() must first be invoked for the containing object.")
	}
	return uint32(v.minVersion)
}

/////////////////////////////////////////////////////////////
// Declaration Data
/////////////////////////////////////////////////////////////

// This struct is embedded in UserDefinedTypeBase, UserDefinedValueBase,
// StructField, UnionField and MojomMethod.
type DeclarationData struct {
	CommentsAttachment
	// A pointer to the DeclaredObject to which this DeclarationData belongs.
	declaredObject DeclaredObject

	attributes         *Attributes
	simpleName         string
	fullyQualifiedName string

	owningFile *MojomFile
	nameToken  lexer.Token

	// If not nil, this field points to the LexicalScope of the declaration
	// corresponding to this data. This will be nil for some embedees
	// of DeclarationData which do not keep track of their scopes.
	scope *Scope

	// We use int64 here because valid ordinals are uint32 and we want to
	// be able to represent an unset value as -1.
	declaredOrdinal int64

	// The zero-based  position of this element within its containing
	// lexical scope as it appears in the Mojom declaration, or -1
	// if this is not set.
	lexicalPosition int32
}

func DeclData(name string, owningFile *MojomFile, nameToken lexer.Token, attributes *Attributes) DeclarationData {
	return DeclDataWithOrdinal(name, owningFile, nameToken, attributes, -1)
}

func DeclDataWithOrdinal(name string, owningFile *MojomFile, nameToken lexer.Token,
	attributes *Attributes, declaredOrdinal int64) DeclarationData {
	return DeclarationData{simpleName: name, owningFile: owningFile, nameToken: nameToken,
		attributes: attributes, declaredOrdinal: declaredOrdinal, lexicalPosition: -1}
}

func (d *DeclarationData) SimpleName() string {
	return d.simpleName
}

func (d *DeclarationData) NameToken() lexer.Token {
	return d.nameToken
}

func (d *DeclarationData) MainToken() *lexer.Token {
	return &d.nameToken
}

func (d *DeclarationData) LineNumber() uint32 {
	return uint32(d.nameToken.LineNo)
}

func (d *DeclarationData) ColumnNumber() uint32 {
	return uint32(d.nameToken.LinePos)
}

func (d *DeclarationData) FullyQualifiedName() string {
	return d.fullyQualifiedName
}

func (d *DeclarationData) Attributes() *Attributes {
	return d.attributes
}

func (d *DeclarationData) DeclaredOrdinal() int64 {
	return d.declaredOrdinal
}

func (d *DeclarationData) LexicalPosition() int32 {
	return d.lexicalPosition
}

func (d *DeclarationData) OwningFile() *MojomFile {
	return d.owningFile
}

func (d *DeclarationData) ContainingType() UserDefinedType {
	if d.scope == nil {
		return nil
	}
	return d.scope.containingType
}

func (d *DeclarationData) DeclaredObject() DeclaredObject {
	return d.declaredObject
}

type Attributes struct {
	CommentsAttachment
	// The attributes are listed in order of occurrence in the source.
	List             []MojomAttribute
	LeftBracketToken lexer.Token
}

func (a *Attributes) String() string {
	if a == nil {
		return "nil"
	}
	return fmt.Sprintf("%s", a.List)
}

func NewAttributes(leftBracket lexer.Token) *Attributes {
	attributes := new(Attributes)
	attributes.List = make([]MojomAttribute, 0)
	attributes.LeftBracketToken = leftBracket
	return attributes
}

func (a *Attributes) MainToken() *lexer.Token {
	return &a.LeftBracketToken
}

type MojomAttribute struct {
	CommentsAttachment
	Key string
	// TODO(rudominer) Decide if we support attribute values as Names.
	// See https://github.com/domokit/mojo/issues/561.
	Value    LiteralValue
	KeyToken *lexer.Token
}

func (ma MojomAttribute) String() string {
	return fmt.Sprintf("%s=%s ", ma.Key, ma.Value)
}

func (ma *MojomAttribute) MainToken() *lexer.Token {
	return ma.KeyToken
}

func NewMojomAttribute(key string, keyToken *lexer.Token, value LiteralValue) (mojomAttribute MojomAttribute) {
	mojomAttribute.KeyToken = keyToken
	mojomAttribute.Key = key
	mojomAttribute.Value = value
	return
}
