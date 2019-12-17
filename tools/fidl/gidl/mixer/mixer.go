// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mixer

import (
	"fmt"
	"math"
	"strings"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
)

// ExtractDeclaration extract the top-level declaration for the provided value,
// and ensures the value conforms to the schema.
func ExtractDeclaration(value interface{}, fidl fidlir.Root) (Declaration, error) {
	decl, err := ExtractDeclarationUnsafe(value, fidl)
	if err != nil {
		return nil, err
	}
	if err := decl.conforms(value); err != nil {
		return nil, fmt.Errorf("value %v failed to conform to declaration (type %T): %v", value, decl, err)
	}
	return decl, nil
}

// ExtractDeclarationUnsafe extract the top-level declaration for the provided value,
// but does not ensure the value conforms to the schema. This is used in cases where
// conformance is too strict (e.g. failure cases).
func ExtractDeclarationUnsafe(value interface{}, fidl fidlir.Root) (Declaration, error) {
	switch value := value.(type) {
	case gidlir.Object:
		decl, ok := schema(fidl).LookupDeclByName(value.Name, false)
		if !ok {
			return nil, fmt.Errorf("unknown declaration %s", value.Name)
		}
		return decl, nil
	default:
		return nil, fmt.Errorf("top-level message must be an object")
	}
}

// ValueVisitor is an API that walks GIDL values.
type ValueVisitor interface {
	OnBool(value bool)
	OnInt64(value int64, typ fidlir.PrimitiveSubtype)
	OnUint64(value uint64, typ fidlir.PrimitiveSubtype)
	OnFloat64(value float64, typ fidlir.PrimitiveSubtype)
	OnString(value string, decl *StringDecl)
	OnStruct(value gidlir.Object, decl *StructDecl)
	OnTable(value gidlir.Object, decl *TableDecl)
	OnXUnion(value gidlir.Object, decl *XUnionDecl)
	OnUnion(value gidlir.Object, decl *UnionDecl)
	OnArray(value []interface{}, decl *ArrayDecl)
	OnVector(value []interface{}, decl *VectorDecl)
	OnNull(decl Declaration)
}

// Visit is the entry point into visiting a value, it dispatches appropriately
// into the visitor.
func Visit(visitor ValueVisitor, value interface{}, decl Declaration) {
	switch value := value.(type) {
	case bool:
		visitor.OnBool(value)
	case int64:
		visitor.OnInt64(value, decl.(PrimitiveDeclaration).Subtype())
	case uint64:
		visitor.OnUint64(value, decl.(PrimitiveDeclaration).Subtype())
	case float64:
		visitor.OnFloat64(value, decl.(PrimitiveDeclaration).Subtype())
	case string:
		switch decl := decl.(type) {
		case *StringDecl:
			visitor.OnString(value, decl)
		default:
			panic(fmt.Sprintf("string value has non-string decl: %T", decl))
		}
	case gidlir.Object:
		switch decl := decl.(type) {
		case *StructDecl:
			visitor.OnStruct(value, decl)
		case *TableDecl:
			visitor.OnTable(value, decl)
		case *XUnionDecl:
			visitor.OnXUnion(value, decl)
		case *UnionDecl:
			visitor.OnUnion(value, decl)
		default:
			panic(fmt.Sprintf("expected %T, got %T: %v", decl, value, value))
		}
	case []interface{}:
		switch decl := decl.(type) {
		case *ArrayDecl:
			visitor.OnArray(value, decl)
		case *VectorDecl:
			visitor.OnVector(value, decl)
		default:
			panic(fmt.Sprintf("not implemented: %T", decl))
		}
	case nil:
		if !decl.IsNullable() {
			panic(fmt.Sprintf("got nil for non-nullable type: %T", decl))
		}
		visitor.OnNull(decl)
	default:
		panic(fmt.Sprintf("not implemented: %T", value))
	}
}

// Declaration describes a FIDL declaration.
type Declaration interface {
	// IsNullable returns true for nullable types. For example, it returns false
	// for string and true for string?.
	IsNullable() bool

	// conforms verifies that the value conforms to this declaration.
	conforms(value interface{}) error
}

// Assert that wrappers conform to the Declaration interface.
var _ = []Declaration{
	&BoolDecl{},
	&NumberDecl{},
	&FloatDecl{},
	&StringDecl{},
	&StructDecl{},
	&TableDecl{},
	&UnionDecl{},
	&XUnionDecl{},
	&ArrayDecl{},
	&VectorDecl{},
}

type PrimitiveDeclaration interface {
	Declaration
	// Subtype returns the primitive subtype (bool, uint32, float64, etc.).
	Subtype() fidlir.PrimitiveSubtype
}

// Assert that wrappers conform to the PrimitiveDeclaration interface.
var _ = []PrimitiveDeclaration{
	&BoolDecl{},
	&NumberDecl{},
	&FloatDecl{},
}

type KeyedDeclaration interface {
	Declaration
	// ForKey looks up the declaration for a specific key.
	ForKey(key gidlir.FieldKey) (Declaration, bool)
}

// Assert that wrappers conform to the KeyedDeclaration interface.
var _ = []KeyedDeclaration{
	&StructDecl{},
	&TableDecl{},
	&UnionDecl{},
	&XUnionDecl{},
}

type ListDeclaration interface {
	Declaration
	// Returns the declaration of the child element (e.g. []int decl -> int decl).
	Elem() (Declaration, bool)
}

// Assert that wrappers conform to the ListDeclaration interface.
var _ = []ListDeclaration{
	&ArrayDecl{},
	&VectorDecl{},
}

// Helper struct for implementing IsNullable on types that are never nullable.
type NeverNullable struct{}

func (NeverNullable) IsNullable() bool {
	return false
}

type BoolDecl struct {
	NeverNullable
}

func (decl *BoolDecl) Subtype() fidlir.PrimitiveSubtype {
	return fidlir.Bool
}

func (decl *BoolDecl) conforms(value interface{}) error {
	switch value.(type) {
	default:
		return fmt.Errorf("expecting bool, found %T (%s)", value, value)
	case bool:
		return nil
	}
}

type NumberDecl struct {
	NeverNullable
	Typ   fidlir.PrimitiveSubtype
	lower int64
	upper uint64
}

func (decl *NumberDecl) Subtype() fidlir.PrimitiveSubtype {
	return decl.Typ
}

func (decl *NumberDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting int64 or uint64, found %T (%s)", value, value)
	case int64:
		if value < 0 {
			if value < decl.lower {
				return fmt.Errorf("out-of-bounds %d", value)
			}
		} else {
			if decl.upper < uint64(value) {
				return fmt.Errorf("out-of-bounds %d", value)
			}
		}
		return nil
	case uint64:
		if decl.upper < value {
			return fmt.Errorf("out-of-bounds %d", value)
		}
		return nil
	}
}

type FloatDecl struct {
	NeverNullable
	Typ fidlir.PrimitiveSubtype
}

func (decl *FloatDecl) Subtype() fidlir.PrimitiveSubtype {
	return decl.Typ
}

func (decl *FloatDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting float64, found %T (%s)", value, value)
	case float64:
		// TODO(fxb/43020): Allow these once each backend supports them.
		if math.IsNaN(value) {
			return fmt.Errorf("NaN not supported: %v", value)
		}
		if math.IsInf(value, 0) {
			return fmt.Errorf("infinity not supported: %v", value)
		}
		return nil
	}
}

type StringDecl struct {
	bound    *int
	nullable bool
}

func (decl *StringDecl) IsNullable() bool {
	return decl.nullable
}

func (decl *StringDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting string, found %T (%s)", value, value)
	case string:
		if decl.bound == nil {
			return nil
		}
		if bound := *decl.bound; bound < len(value) {
			return fmt.Errorf(
				"string '%s' is over bounds, expecting %d but was %d", value,
				bound, len(value))
		}
		return nil
	case nil:
		if decl.nullable {
			return nil
		}
		return fmt.Errorf("expecting non-null string, found nil")
	}
}

// StructDecl describes a struct declaration.
type StructDecl struct {
	fidlir.Struct
	schema   schema
	nullable bool
}

func (decl *StructDecl) IsNullable() bool {
	return decl.nullable
}

func (decl *StructDecl) MemberType(key gidlir.FieldKey) (fidlir.Type, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key.Name {
			return member.Type, true
		}
	}
	return fidlir.Type{}, false
}

// TODO(bprosnitz) Don't repeat this across types.
func (decl *StructDecl) ForKey(key gidlir.FieldKey) (Declaration, bool) {
	if typ, ok := decl.MemberType(key); ok {
		return decl.schema.LookupDeclByType(typ)
	}
	return nil, false
}

// objectConforms is a helper function for implementing Declarations.conforms on
// types that expect a gidlir.Object value. It takes the kind ("struct", etc.),
// expected type name, schema, and nullability, and returns the object or an
// error. It can also return (nil, nil) when value is nil and nullable is true.
func objectConforms(value interface{}, kind string, name fidlir.EncodedCompoundIdentifier, schema schema, nullable bool) (*gidlir.Object, error) {
	switch value := value.(type) {
	default:
		return nil, fmt.Errorf("expecting %s, found %T (%v)", kind, value, value)
	case gidlir.Object:
		if actualName := schema.name(value.Name); actualName != name {
			return nil, fmt.Errorf("expecting %s %s, found %s", kind, name, actualName)
		}
		return &value, nil
	case nil:
		if nullable {
			return nil, nil
		}
		return nil, fmt.Errorf("expecting non-null %s %s, found nil", kind, name)
	}
}

func (decl *StructDecl) conforms(value interface{}) error {
	object, err := objectConforms(value, "struct", decl.Name, decl.schema, decl.nullable)
	if err != nil {
		return err
	}
	if object == nil {
		return nil
	}
	for _, field := range object.Fields {
		if fieldDecl, ok := decl.ForKey(field.Key); !ok {
			return fmt.Errorf("field %s: unknown", field.Key.Name)
		} else if err := fieldDecl.conforms(field.Value); err != nil {
			return fmt.Errorf("field %s: %s", field.Key.Name, err)
		}
	}
	return nil
}

// TableDecl describes a table declaration.
type TableDecl struct {
	NeverNullable
	fidlir.Table
	schema schema
}

func (decl *TableDecl) MemberType(key gidlir.FieldKey) (fidlir.Type, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key.Name || uint64(member.Ordinal) == key.Ordinal {
			return member.Type, true
		}
	}
	return fidlir.Type{}, false
}

// TODO(bprosnitz) Don't repeat this across types.
func (decl *TableDecl) ForKey(key gidlir.FieldKey) (Declaration, bool) {
	if typ, ok := decl.MemberType(key); ok {
		return decl.schema.LookupDeclByType(typ)
	}
	return nil, false
}

func (decl *TableDecl) conforms(value interface{}) error {
	object, err := objectConforms(value, "table", decl.Name, decl.schema, false)
	if err != nil {
		return err
	}
	if object == nil {
		panic("tables cannot be nullable")
	}
	for _, field := range object.Fields {
		if field.Key.Name == "" {
			if _, ok := decl.ForKey(field.Key); ok {
				return fmt.Errorf("field name must be used rather than ordinal %d t", field.Key.Ordinal)
			}
			continue
		}
		if fieldDecl, ok := decl.ForKey(field.Key); !ok {
			return fmt.Errorf("field %s: unknown", field.Key.Name)
		} else if err := fieldDecl.conforms(field.Value); err != nil {
			return fmt.Errorf("field %s: %s", field.Key.Name, err)
		}
	}
	return nil
}

// XUnionDecl describes a xunion declaration.
type XUnionDecl struct {
	fidlir.XUnion
	schema   schema
	nullable bool
}

func (decl *XUnionDecl) IsNullable() bool {
	return decl.nullable
}

func (decl *XUnionDecl) MemberType(key gidlir.FieldKey) (fidlir.Type, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key.Name || uint64(member.Ordinal) == key.Ordinal {
			return member.Type, true
		}
	}
	return fidlir.Type{}, false
}

// TODO(bprosnitz) Don't repeat this across types.
func (decl XUnionDecl) ForKey(key gidlir.FieldKey) (Declaration, bool) {
	if typ, ok := decl.MemberType(key); ok {
		return decl.schema.LookupDeclByType(typ)
	}
	return nil, false
}

func (decl XUnionDecl) conforms(value interface{}) error {
	object, err := objectConforms(value, "xunion", decl.Name, decl.schema, decl.nullable)
	if err != nil {
		return err
	}
	if object == nil {
		return nil
	}
	if num := len(object.Fields); num != 1 {
		return fmt.Errorf("must have one field, found %d", num)
	}
	for _, field := range object.Fields {
		if field.Key.Name == "" {
			if _, ok := decl.ForKey(field.Key); ok {
				return fmt.Errorf("field name must be used rather than ordinal %d t", field.Key.Ordinal)
			}
			continue
		}
		if fieldDecl, ok := decl.ForKey(field.Key); !ok {
			return fmt.Errorf("field %s: unknown", field.Key.Name)
		} else if err := fieldDecl.conforms(field.Value); err != nil {
			return fmt.Errorf("field %s: %s", field.Key.Name, err)
		}
	}
	return nil
}

// UnionDecl describes a xunion declaration.
type UnionDecl struct {
	fidlir.Union
	schema   schema
	nullable bool
}

func (decl *UnionDecl) IsNullable() bool {
	return decl.nullable
}

func (decl *UnionDecl) MemberType(key gidlir.FieldKey) (fidlir.Type, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key.Name {
			return member.Type, true
		}
	}
	return fidlir.Type{}, false
}

// TODO(bprosnitz) Don't repeat this across types.
func (decl UnionDecl) ForKey(key gidlir.FieldKey) (Declaration, bool) {
	if typ, ok := decl.MemberType(key); ok {
		return decl.schema.LookupDeclByType(typ)
	}
	return nil, false
}

func (decl UnionDecl) conforms(value interface{}) error {
	object, err := objectConforms(value, "union", decl.Name, decl.schema, decl.nullable)
	if err != nil {
		return err
	}
	if object == nil {
		return nil
	}
	if num := len(object.Fields); num != 1 {
		return fmt.Errorf("must have one field, found %d", num)
	}
	for _, field := range object.Fields {
		if fieldDecl, ok := decl.ForKey(field.Key); !ok {
			return fmt.Errorf("field %s: unknown", field.Key.Name)
		} else if err := fieldDecl.conforms(field.Value); err != nil {
			return fmt.Errorf("field %s: %s", field.Key.Name, err)
		}
	}
	return nil
}

type ArrayDecl struct {
	NeverNullable
	schema schema
	// The array has type `typ`, and it contains `typ.ElementType` elements.
	typ fidlir.Type
}

func (decl ArrayDecl) Elem() (Declaration, bool) {
	return decl.schema.LookupDeclByType(*decl.typ.ElementType)
}

func (decl ArrayDecl) Size() int {
	return *decl.typ.ElementCount
}

func (decl ArrayDecl) conforms(untypedValue interface{}) error {
	switch value := untypedValue.(type) {
	default:
		return fmt.Errorf("expecting array, found %T (%v)", untypedValue, untypedValue)
	case []interface{}:
		if len(value) != decl.Size() {
			return fmt.Errorf("expecting %d elements, got %d", decl.Size(), len(value))
		}
		elemDecl, ok := decl.Elem()
		if !ok {
			return fmt.Errorf("error resolving elem declaration")
		}
		for i, elem := range value {
			if err := elemDecl.conforms(elem); err != nil {
				return fmt.Errorf("[%d]: %s", i, err)
			}
		}
		return nil
	}
}

type VectorDecl struct {
	schema schema
	// The vector has type `typ`, and it contains `typ.ElementType` elements.
	typ fidlir.Type
}

func (decl VectorDecl) IsNullable() bool {
	return decl.typ.Nullable
}

func (decl VectorDecl) Elem() (Declaration, bool) {
	return decl.schema.LookupDeclByType(*decl.typ.ElementType)
}

func (decl VectorDecl) MaxSize() (int, bool) {
	if decl.typ.ElementCount != nil {
		return *decl.typ.ElementCount, true
	}
	return 0, false
}

func (decl VectorDecl) conforms(untypedValue interface{}) error {
	switch value := untypedValue.(type) {
	default:
		return fmt.Errorf("expecting vector, found %T (%v)", untypedValue, untypedValue)
	case []interface{}:
		if maxSize, ok := decl.MaxSize(); ok && len(value) > maxSize {
			return fmt.Errorf("expecting at most %d elements, got %d", maxSize, len(value))
		}
		elemDecl, ok := decl.Elem()
		if !ok {
			return fmt.Errorf("error resolving elem declaration")
		}
		for i, elem := range value {
			if err := elemDecl.conforms(elem); err != nil {
				return fmt.Errorf("[%d]: %s", i, err)
			}
		}
		return nil
	case nil:
		if decl.typ.Nullable {
			return nil
		}
		return fmt.Errorf("expecting non-nullable vector, got nil")
	}
}

type schema fidlir.Root

// LookupDeclByName looks up a message declaration by name.
func (s schema) LookupDeclByName(name string, nullable bool) (Declaration, bool) {
	for _, decl := range s.Structs {
		if decl.Name == s.name(name) {
			return &StructDecl{
				Struct:   decl,
				schema:   s,
				nullable: nullable,
			}, true
		}
	}
	for _, decl := range s.Tables {
		if decl.Name == s.name(name) {
			if nullable {
				panic(fmt.Sprintf("nullable table %s is not allowed", name))
			}
			return &TableDecl{
				Table:  decl,
				schema: s,
			}, true
		}
	}
	for _, decl := range s.XUnions {
		if decl.Name == s.name(name) {
			return &XUnionDecl{
				XUnion:   decl,
				schema:   s,
				nullable: nullable,
			}, true
		}
	}
	for _, decl := range s.Unions {
		if decl.Name == s.name(name) {
			return &UnionDecl{
				Union:    decl,
				schema:   s,
				nullable: nullable,
			}, true
		}
	}
	// TODO(pascallouis): add support missing declarations
	return nil, false
}

// LookupDeclByType looks up a message declaration by type.
func (s schema) LookupDeclByType(typ fidlir.Type) (Declaration, bool) {
	switch typ.Kind {
	case fidlir.StringType:
		return &StringDecl{
			bound:    typ.ElementCount,
			nullable: typ.Nullable,
		}, true
	case fidlir.PrimitiveType:
		switch typ.PrimitiveSubtype {
		case fidlir.Bool:
			return &BoolDecl{}, true
		case fidlir.Int8:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: math.MinInt8, upper: math.MaxInt8}, true
		case fidlir.Int16:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: math.MinInt16, upper: math.MaxInt16}, true
		case fidlir.Int32:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: math.MinInt32, upper: math.MaxInt32}, true
		case fidlir.Int64:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: math.MinInt64, upper: math.MaxInt64}, true
		case fidlir.Uint8:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint8}, true
		case fidlir.Uint16:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint16}, true
		case fidlir.Uint32:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint32}, true
		case fidlir.Uint64:
			return &NumberDecl{Typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint64}, true
		case fidlir.Float32:
			return &FloatDecl{Typ: typ.PrimitiveSubtype}, true
		case fidlir.Float64:
			return &FloatDecl{Typ: typ.PrimitiveSubtype}, true
		default:
			panic(fmt.Sprintf("unsupported primitive subtype: %s", typ.PrimitiveSubtype))
		}
	case fidlir.IdentifierType:
		parts := strings.Split(string(typ.Identifier), "/")
		if len(parts) != 2 {
			panic(fmt.Sprintf("malformed identifier: %s", typ.Identifier))
		}
		return s.LookupDeclByName(parts[1], typ.Nullable)
	case fidlir.ArrayType:
		return &ArrayDecl{schema: s, typ: typ}, true
	case fidlir.VectorType:
		return &VectorDecl{schema: s, typ: typ}, true
	default:
		// TODO(pascallouis): many more cases.
		panic("not implemented")
	}
}

func (s schema) name(name string) fidlir.EncodedCompoundIdentifier {
	return fidlir.EncodedCompoundIdentifier(fmt.Sprintf("%s/%s", s.Name, name))
}
