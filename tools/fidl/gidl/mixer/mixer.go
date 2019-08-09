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
		return nil, err
	}
	return decl, nil
}

// ExtractDeclarationUnsafe extract the top-level declaration for the provided value,
// but does not ensure the value conforms to the schema. This is used in cases where
// conformance is too strict (e.g. failure cases).
func ExtractDeclarationUnsafe(value interface{}, fidl fidlir.Root) (Declaration, error) {
	switch value := value.(type) {
	case gidlir.Object:
		decl, ok := schema(fidl).LookupDeclByName(value.Name)
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
	OnString(value string)
	OnStruct(value gidlir.Object, decl *StructDecl)
	OnTable(value gidlir.Object, decl *TableDecl)
	OnXUnion(value gidlir.Object, decl *XUnionDecl)
	OnUnion(value gidlir.Object, decl *UnionDecl)
	OnArray(value []interface{}, decl *ArrayDecl)
	OnVector(value []interface{}, decl *VectorDecl)
}

// Visit is the entry point into visiting a value, it dispatches appropriately
// into the visitor.
func Visit(visitor ValueVisitor, value interface{}, decl Declaration) {
	switch value := value.(type) {
	case bool:
		visitor.OnBool(value)
	case int64:
		visitor.OnInt64(value, extractSubtype(decl))
	case uint64:
		visitor.OnUint64(value, extractSubtype(decl))
	case string:
		visitor.OnString(value)
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
			panic(fmt.Sprintf("not implemented: %T", decl))
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
	default:
		panic(fmt.Sprintf("not implemented: %T", value))
	}
}

func extractSubtype(decl Declaration) fidlir.PrimitiveSubtype {
	switch decl := decl.(type) {
	case *NumberDecl:
		return decl.Typ
	default:
		panic("should not be reachable, there must be a bug somewhere")
	}
}

// Declaration describes a FIDL declaration.
type Declaration interface {
	// conforms verifies that the value conforms to this declaration.
	conforms(value interface{}) error
}

// Assert that wrappers conform to the Declaration interface.
var _ = []Declaration{
	&BoolDecl{},
	&NumberDecl{},
	&StringDecl{},
	&StructDecl{},
	&TableDecl{},
	&XUnionDecl{},
	&ArrayDecl{},
	&VectorDecl{},
}

type KeyedDeclaration interface {
	Declaration
	// ForKey looks up the declaration for a specific key.
	ForKey(key string) (Declaration, bool)
}

// Assert that wrappers conform to the KeyedDeclaration interface.
var _ = []KeyedDeclaration{
	&StructDecl{},
	&TableDecl{},
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

type BoolDecl struct {
}

func (decl *BoolDecl) conforms(value interface{}) error {
	switch value.(type) {
	default:
		return fmt.Errorf("expecting number, found %T (%s)", value, value)
	case bool:
		return nil
	}
}

type NumberDecl struct {
	Typ   fidlir.PrimitiveSubtype
	lower int64
	upper uint64
}

func (decl *NumberDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting number, found %T (%s)", value, value)
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

type StringDecl struct {
	bound *int
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
	}
}

// StructDecl describes a struct declaration.
type StructDecl struct {
	fidlir.Struct
	schema schema
}

// ForKey retrieves a declaration for a key.
func (decl *StructDecl) ForKey(key string) (Declaration, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key {
			return decl.schema.LookupDeclByType(member.Type)
		}
	}
	return nil, false
}

// IsKeyNullable indicates whether this key is optional, i.e. whether it
// represents an optional field.
func (decl *StructDecl) IsKeyNullable(key string) bool {
	for _, member := range decl.Members {
		if string(member.Name) == key {
			return member.Type.Nullable
		}
	}
	return false
}

func (decl *StructDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting string, found %T (%v)", value, value)
	case gidlir.Object:
		for _, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(field.Name); !ok {
				return fmt.Errorf("field %s: unknown", field.Name)
			} else if err := fieldDecl.conforms(field.Value); err != nil {
				return fmt.Errorf("field %s: %s", field.Name, err)
			}
		}
		return nil
	}
}

// TableDecl describes a table declaration.
type TableDecl struct {
	fidlir.Table
	schema schema
}

// ForKey retrieves a declaration for a key.
func (decl *TableDecl) ForKey(key string) (Declaration, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key {
			return decl.schema.LookupDeclByType(member.Type)
		}
	}
	return nil, false
}

func (decl *TableDecl) conforms(untypedValue interface{}) error {
	switch value := untypedValue.(type) {
	default:
		return fmt.Errorf("expecting object, found %T (%v)", untypedValue, untypedValue)
	case gidlir.Object:
		for _, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(field.Name); !ok {
				return fmt.Errorf("field %s: unknown", field.Name)
			} else if err := fieldDecl.conforms(field.Value); err != nil {
				return fmt.Errorf("field %s: %s", field.Name, err)
			}
		}
		return nil
	}
}

// XUnionDecl describes a xunion declaration.
type XUnionDecl struct {
	fidlir.XUnion
	schema schema
}

// ForKey retrieves a declaration for a key.
func (decl XUnionDecl) ForKey(key string) (Declaration, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key {
			return decl.schema.LookupDeclByType(member.Type)
		}
	}
	return nil, false
}

func (decl XUnionDecl) conforms(untypedValue interface{}) error {
	switch value := untypedValue.(type) {
	default:
		return fmt.Errorf("expecting object, found %T (%v)", untypedValue, untypedValue)
	case gidlir.Object:
		if num := len(value.Fields); num != 1 {
			return fmt.Errorf("must have one field, found %d", num)
		}
		for _, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(field.Name); !ok {
				return fmt.Errorf("field %s: unknown", field.Name)
			} else if err := fieldDecl.conforms(field.Value); err != nil {
				return fmt.Errorf("field %s: %s", field.Name, err)
			}
		}
		return nil
	}
}

// UnionDecl describes a xunion declaration.
type UnionDecl struct {
	fidlir.Union
	schema schema
}

// ForKey retrieves a declaration for a key.
func (decl UnionDecl) ForKey(key string) (Declaration, bool) {
	for _, member := range decl.Members {
		if string(member.Name) == key {
			return decl.schema.LookupDeclByType(member.Type)
		}
	}
	return nil, false
}

func (decl UnionDecl) conforms(untypedValue interface{}) error {
	switch value := untypedValue.(type) {
	default:
		return fmt.Errorf("expecting object, found %T (%v)", untypedValue, untypedValue)
	case gidlir.Object:
		if num := len(value.Fields); num != 1 {
			return fmt.Errorf("must have one field, found %d", num)
		}
		for _, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(field.Name); !ok {
				return fmt.Errorf("field %s: unknown", field.Name)
			} else if err := fieldDecl.conforms(field.Value); err != nil {
				return fmt.Errorf("field %s: %s", field.Name, err)
			}
		}
		return nil
	}
}

type ArrayDecl struct {
	schema schema
	typ    fidlir.Type
}

func (decl ArrayDecl) Elem() (Declaration, bool) {
	return decl.schema.LookupDeclByType(*decl.typ.ElementType)
}

func (decl ArrayDecl) Size() int {
	return *decl.typ.ElementCount
}

func (decl ArrayDecl) conforms(untypedValue interface{}) error {
	if list, ok := untypedValue.([]interface{}); ok {
		if len(list) > decl.Size() {
			return fmt.Errorf("%d elements exceeds limits of an array of length %d", len(list), decl.Size())
		}
	} else {
		return fmt.Errorf("expecting []interface{}, got %T (%v)", untypedValue, untypedValue)
	}
	if _, ok := decl.Elem(); !ok {
		return fmt.Errorf("error resolving elem declaration")
	}
	return nil
}

type VectorDecl struct {
	schema schema
	typ    fidlir.Type
}

func (decl VectorDecl) Elem() (Declaration, bool) {
	return decl.schema.LookupDeclByType(*decl.typ.ElementType)
}

func (decl VectorDecl) conforms(untypedValue interface{}) error {
	if _, ok := untypedValue.([]interface{}); !ok {
		return fmt.Errorf("expecting []interface{}, got %T (%v)", untypedValue, untypedValue)
	}
	if _, ok := decl.Elem(); !ok {
		return fmt.Errorf("error resolving elem declaration")
	}
	return nil
}

type schema fidlir.Root

// LookupDeclByName looks up a message declaration by name.
func (s schema) LookupDeclByName(name string) (Declaration, bool) {
	for _, decl := range s.Structs {
		if decl.Name == s.name(name) {
			return &StructDecl{
				Struct: decl,
				schema: s,
			}, true
		}
	}
	for _, decl := range s.Tables {
		if decl.Name == s.name(name) {
			return &TableDecl{
				Table:  decl,
				schema: s,
			}, true
		}
	}
	for _, decl := range s.XUnions {
		if decl.Name == s.name(name) {
			return &XUnionDecl{
				XUnion: decl,
				schema: s,
			}, true
		}
	}
	for _, decl := range s.Unions {
		if decl.Name == s.name(name) {
			return &UnionDecl{
				Union:  decl,
				schema: s,
			}, true
		}
	}
	// TODO(pascallouis): add support missing declarations (e.g. xunions)
	return nil, false
}

// LookupDeclByType looks up a message declaration by type.
func (s schema) LookupDeclByType(typ fidlir.Type) (Declaration, bool) {
	switch typ.Kind {
	case fidlir.StringType:
		return &StringDecl{
			bound: typ.ElementCount,
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
		default:
			panic(fmt.Sprintf("unsupported primitive subtype: %s", typ.PrimitiveSubtype))
		}
	case fidlir.IdentifierType:
		parts := strings.Split(string(typ.Identifier), "/")
		if len(parts) != 2 {
			panic(fmt.Sprintf("malformed identifier: %s", typ.Identifier))
		}
		return s.LookupDeclByName(parts[1])
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
