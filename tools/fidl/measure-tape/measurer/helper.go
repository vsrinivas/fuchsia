// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"
	"log"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
)

type keyedDecl struct {
	key      string
	nullable bool
	decl     interface{}
}

type primitiveDecl struct {
	size int
}
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
		return keyedDecl{
			decl: arrayDecl{
				elementCount: *typ.ElementCount,
				elementDecl:  m.toDecl(*typ.ElementType),
			},
		}
	case fidlir.VectorType:
		// TODO(fxb/49480): Support measuring vectors.
		return keyedDecl{key: "", decl: vectorDecl{
			elementDecl: m.toDecl(*typ.ElementType),
		}}
	case fidlir.StringType:
		return keyedDecl{
			nullable: typ.Nullable,
			decl:     stringDecl{},
		}
	case fidlir.HandleType:
		fallthrough
	case fidlir.RequestType:
		return keyedDecl{
			decl:     handleDecl{},
			nullable: typ.Nullable,
		}
	case fidlir.PrimitiveType:
		return keyedDecl{
			decl: primitiveDecl{size: toSize(typ.PrimitiveSubtype)},
		}
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
			return keyedDecl{
				key:  fqn,
				decl: primitiveDecl{size: toSize(decl.Type)},
			}, true
		}
	}
	for _, decl := range root.Bits {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{
				key:  fqn,
				decl: primitiveDecl{size: toSize(decl.Type.PrimitiveSubtype)},
			}, true
		}
	}
	for _, decl := range root.Interfaces {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: handleDecl{}}, true
		}
	}
	return keyedDecl{}, false
}

func toSize(subtype fidlir.PrimitiveSubtype) int {
	switch subtype {
	case fidlir.Bool, fidlir.Int8, fidlir.Uint8:
		return 1
	case fidlir.Int16, fidlir.Uint16:
		return 2
	case fidlir.Int32, fidlir.Uint32, fidlir.Float32:
		return 4
	case fidlir.Int64, fidlir.Uint64, fidlir.Float64:
		return 8
	default:
		panic(fmt.Sprintf("unknown subtype: %v", subtype))
		return 0
	}
}
