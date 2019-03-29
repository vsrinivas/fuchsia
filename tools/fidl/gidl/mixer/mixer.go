// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mixer

import (
	"fmt"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
)

// ExtractDeclaration extract the top-level declaration for the provided value,
// and ensures the value conforms to the schema.
func ExtractDeclaration(untypedValue interface{}, fidl fidlir.Root) (Declaration, error) {
	switch value := untypedValue.(type) {
	case gidlir.Object:
		decl, ok := schema(fidl).LookupDeclByName(value.Name)
		if !ok {
			return nil, fmt.Errorf("unknown declaration %s", value.Name)
		}
		err := decl.conforms(untypedValue)
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
	OnString(value string)
	OnStruct(value gidlir.Object, decl *StructDecl)
}

// Visit is the entry point into visiting a value, it dispatches appropriately
// into the visitor.
func Visit(visitor ValueVisitor, value interface{}, decl Declaration) {
	switch value := value.(type) {
	case string:
		visitor.OnString(value)
	case gidlir.Object:
		switch decl := decl.(type) {
		case *StructDecl:
			visitor.OnStruct(value, decl)
		}
	default:
		panic(fmt.Sprintf("not implemented: %T", value))
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
	&stringDecl{},
	&StructDecl{},
}

type stringDecl struct {
	bound int
}

func (decl *stringDecl) ForKey(key string) (Declaration, bool) {
	return nil, false
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
	// TODO(pascallouis): many more cases.
	default:
		return nil, false
	}
}

func (s schema) name(name string) fidlir.EncodedCompoundIdentifier {
	return fidlir.EncodedCompoundIdentifier(fmt.Sprintf("%s/%s", s.Name, name))
}
