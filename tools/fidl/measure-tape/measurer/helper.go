// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"log"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
)

type keyedDecl struct {
	key      string
	nullable bool
	decl     interface{}
}

type primitiveDecl struct{}
type handleDecl struct{}
type vectorDecl struct {
	elementDecl keyedDecl
}
type stringDecl struct{}
type arrayDecl struct {
	elementCount int
	elementDecl  keyedDecl
}

func (m *Measurer) toDecl(typ fidlir.Type) keyedDecl {
	switch typ.Kind {
	case fidlir.ArrayType:
		// TODO(fxb/49480): Support measuring arrays.
		return keyedDecl{key: "", decl: arrayDecl{
			elementCount: *typ.ElementCount,
			elementDecl:  m.toDecl(*typ.ElementType),
		}}
	case fidlir.VectorType:
		// TODO(fxb/49480): Support measuring vectors.
		return keyedDecl{key: "", decl: vectorDecl{
			elementDecl: m.toDecl(*typ.ElementType),
		}}
	case fidlir.StringType:
		return keyedDecl{
			key:      "",
			nullable: typ.Nullable,
			decl:     stringDecl{},
		}
	case fidlir.HandleType:
		fallthrough
	case fidlir.RequestType:
		return keyedDecl{
			key:      "",
			decl:     handleDecl{},
			nullable: typ.Nullable,
		}
	case fidlir.PrimitiveType:
		return keyedDecl{key: "", decl: primitiveDecl{}}
	case fidlir.IdentifierType:
		kd, ok := m.lookup(fidlcommon.MustReadName(string(typ.Identifier)))
		if !ok {
			log.Panicf("%v", typ)
		}
		kd.nullable = typ.Nullable
		return kd
	default:
		log.Panic("not reachable")
		return keyedDecl{}
	}
}

func (m *Measurer) lookup(name fidlcommon.Name) (keyedDecl, bool) {
	root, ok := m.roots[name.LibraryName()]
	if !ok {
		return keyedDecl{}, false
	}
	fqn := name.FullyQualifiedName()
	for _, decl := range root.Structs {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, true
		}
	}
	for _, decl := range root.Tables {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, true
		}
	}
	for _, decl := range root.Unions {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, true
		}
	}
	for _, decl := range root.Enums {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: primitiveDecl{}}, true
		}
	}
	for _, decl := range root.Bits {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: primitiveDecl{}}, true
		}
	}
	for _, decl := range root.Interfaces {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: handleDecl{}}, true
		}
	}
	return keyedDecl{}, false
}
