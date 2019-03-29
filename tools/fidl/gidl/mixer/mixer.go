// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mixer

import (
	"fmt"
	"math"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
)

// ExtractDeclaration extract the top-level declaration for the provided value,
// and ensures the value conforms to the schema.
func ExtractDeclaration(value interface{}, fidl fidlir.Root) (Declaration, error) {
	switch value := value.(type) {
	case gidlir.Object:
		decl, ok := schema(fidl).LookupDeclByName(value.Name)
		if !ok {
			return nil, fmt.Errorf("unknown declaration %s", value.Name)
		}
		err := decl.conforms(value)
		if err != nil {
			return nil, err
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
		default:
			panic(fmt.Sprintf("not implemented: %T", decl))
		}
	default:
		panic(fmt.Sprintf("not implemented: %T", value))
	}
}

func extractSubtype(decl Declaration) fidlir.PrimitiveSubtype {
	switch decl := decl.(type) {
	case *numberDecl:
		return decl.typ
	default:
		panic("should not be reachable, there must be a bug somewhere")
	}
}

// Declaration describes a FIDL declaration.
type Declaration interface {
	// ForKey looks up the declaration for a specific key.
	ForKey(key string) (Declaration, bool)

	// conforms verifies that the value conforms to this declaration.
	conforms(value interface{}) error
}

// Assert that wrappers conform to the Declaration interface.
var _ = []Declaration{
	&boolDecl{},
	&numberDecl{},
	&stringDecl{},
	&StructDecl{},
	&TableDecl{},
}

type hasNoKey struct{}

func (decl hasNoKey) ForKey(key string) (Declaration, bool) {
	return nil, false
}

type boolDecl struct {
	hasNoKey
}

func (decl *boolDecl) conforms(value interface{}) error {
	switch value.(type) {
	default:
		return fmt.Errorf("expecting number, found %T (%s)", value, value)
	case bool:
		return nil
	}
}

type numberDecl struct {
	hasNoKey
	typ   fidlir.PrimitiveSubtype
	lower int64
	upper uint64
}

func (decl *numberDecl) conforms(value interface{}) error {
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

type stringDecl struct {
	hasNoKey
	bound int
}

func (decl *stringDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting string, found %T (%s)", value, value)
	case string:
		if decl.bound < len(value) {
			return fmt.Errorf(
				"string '%s' is over bounds, expecting %d but was %d", value,
				decl.bound, len(value))
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

func (decl *StructDecl) conforms(value interface{}) error {
	switch value := value.(type) {
	default:
		return fmt.Errorf("expecting string, found %T (%v)", value, value)
	case gidlir.Object:
		for key, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(key); !ok {
				return fmt.Errorf("field %s: unknown", key)
			} else if err := fieldDecl.conforms(field); err != nil {
				return fmt.Errorf("field %s: %s", key, err)
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
		for key, field := range value.Fields {
			if fieldDecl, ok := decl.ForKey(key); !ok {
				return fmt.Errorf("field %s: unknown", key)
			} else if err := fieldDecl.conforms(field); err != nil {
				return fmt.Errorf("field %s: %s", key, err)
			}
		}
		return nil
	}
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
	// TODO(pascallouis): add support missing declarations (e.g. xunions)
	return nil, false
}

// LookupDeclByType looks up a message declaration by type.
func (s schema) LookupDeclByType(typ fidlir.Type) (Declaration, bool) {
	switch typ.Kind {
	case fidlir.StringType:
		return &stringDecl{
			bound: *typ.ElementCount,
		}, true
	case fidlir.PrimitiveType:
		switch typ.PrimitiveSubtype {
		case fidlir.Bool:
			return &boolDecl{}, true
		case fidlir.Int8:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: math.MinInt8, upper: math.MaxInt8}, true
		case fidlir.Int16:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: math.MinInt16, upper: math.MaxInt16}, true
		case fidlir.Int32:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: math.MinInt32, upper: math.MaxInt32}, true
		case fidlir.Int64:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: math.MinInt64, upper: math.MaxInt64}, true
		case fidlir.Uint8:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint8}, true
		case fidlir.Uint16:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint16}, true
		case fidlir.Uint32:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint32}, true
		case fidlir.Uint64:
			return &numberDecl{typ: typ.PrimitiveSubtype, lower: 0, upper: math.MaxUint64}, true
		default:
			panic(fmt.Sprintf("unsupported primitive subtype: %s", typ.PrimitiveSubtype))
		}
	default:
		// TODO(pascallouis): many more cases.
		panic("not implemented")
	}
}

func (s schema) name(name string) fidlir.EncodedCompoundIdentifier {
	return fidlir.EncodedCompoundIdentifier(fmt.Sprintf("%s/%s", s.Name, name))
}
