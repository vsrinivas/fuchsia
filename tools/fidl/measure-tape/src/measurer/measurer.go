// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"
	"log"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

type Measurer struct {
	roots map[fidlcommon.LibraryName]fidlir.Root
	tapes map[string]*MeasuringTape
}

func (m *Measurer) RootLibraries() []fidlcommon.LibraryName {
	var libraryNames []fidlcommon.LibraryName
	for libraryName := range m.roots {
		libraryNames = append(libraryNames, libraryName)
	}
	return libraryNames
}

func NewMeasurer(roots []fidlir.Root) *Measurer {
	m := &Measurer{
		roots: make(map[fidlcommon.LibraryName]fidlir.Root),
		tapes: make(map[string]*MeasuringTape),
	}
	for _, root := range roots {
		m.roots[fidlcommon.MustReadLibraryName(string(root.Name))] = root
	}
	return m
}

type TapeKind int

const (
	_ TapeKind = iota

	Struct
	Union
	Table
	Vector
	String
	Array
	Handle
	Primitive
)

type MeasuringTape struct {
	kind TapeKind

	// name stores the name of the declaration if relevant, i.e. Union, Struct, Table
	name fidlcommon.Name
	decl interface{}

	inlineNumBytes int

	// hasHandles indicates whether this measuring tape may have a handle.
	hasHandles bool

	// hasOutOfLine indicates whether this measuring tape may have out-of-line data.
	hasOutOfLine bool

	// inlineNumHandles indicates the number of inline handles, and can only be
	// used as an upper bound for measuring tapes which are inline only i.e.
	// `hasOutOfLine` is `false`
	inlineNumHandles int

	// - struct: struct members
	// - table: table members indexed by ordinal, so that max_set_ordinal * envelope can be calculated
	// - union: union members, only one selected
	members []measuringTapeMember

	// nullable for strings, unions, structs
	nullable bool

	// isFlexible for unions
	isFlexible bool

	// elementCount for arrays
	elementCount int

	// elementMt for arrays
	elementMt *MeasuringTape
}

func (mt *MeasuringTape) Name() fidlcommon.Name {
	return mt.name
}

type measuringTapeMember struct {
	name    string
	ordinal int
	mt      *MeasuringTape
}

func (m *Measurer) MeasuringTapeFor(targetType string) (*MeasuringTape, error) {
	name, err := fidlcommon.ReadName(targetType)
	if err != nil {
		return nil, err
	}
	kd, ok := m.lookup(name)
	if !ok {
		return nil, fmt.Errorf("no declaration %s, you may be missing a JSON IR", targetType)
	}
	return m.createMeasuringTape(kd)
}

func (m *Measurer) createMeasuringTape(kd keyedDecl) (*MeasuringTape, error) {
	if tape, ok := m.tapes[kd.key]; ok {
		return tape, nil
	}

	var (
		tape *MeasuringTape
		err  error
	)
	switch decl := kd.decl.(type) {
	case fidlir.Struct:
		tape, err = m.createStructMeasuringTape(decl)
	case fidlir.Table:
		tape, err = m.createTableMeasuringTape(decl)
	case fidlir.Union:
		tape, err = m.createUnionMeasuringTape(decl)
	case primitiveDecl:
		tape = &MeasuringTape{
			kind:           Primitive,
			inlineNumBytes: decl.size,
		}
	case handleDecl:
		tape = &MeasuringTape{
			kind:             Handle,
			hasHandles:       true,
			inlineNumBytes:   toSize(fidlir.Uint32),
			inlineNumHandles: 1,
		}
	case stringDecl:
		tape = &MeasuringTape{
			kind:           String,
			inlineNumBytes: 16, // sizeof(fidl_string_t)
			hasOutOfLine:   true,
		}
	case vectorDecl:
		elementMt, err := m.createMeasuringTape(decl.elementDecl)
		if err != nil {
			return nil, err
		}
		tape = &MeasuringTape{
			kind:           Vector,
			hasHandles:     elementMt.hasHandles,
			elementMt:      elementMt,
			inlineNumBytes: 16, // sizeof(fidl_vector_t)
			hasOutOfLine:   true,
		}
	case arrayDecl:
		elementMt, err := m.createMeasuringTape(decl.elementDecl)
		if err != nil {
			return nil, err
		}
		tape = &MeasuringTape{
			kind:             Array,
			hasHandles:       elementMt.hasHandles,
			elementCount:     decl.elementCount,
			elementMt:        elementMt,
			inlineNumBytes:   decl.elementCount * elementMt.inlineNumBytes,
			hasOutOfLine:     elementMt.hasOutOfLine,
			inlineNumHandles: decl.elementCount * elementMt.inlineNumHandles,
		}
	default:
		log.Panicf("unexpected decl, was %+v", kd.decl)
	}
	if err != nil {
		return nil, err
	}
	tape.nullable = kd.nullable
	if len(kd.key) != 0 {
		m.tapes[kd.key] = tape
	}
	return tape, nil
}

func (m *Measurer) createStructMeasuringTape(decl fidlir.Struct) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.Members {
		// all primitives including bits & enums are sized as part of the inline cost
		// string/vector have 8 bytes as part of inline cost, rest is out-of-line (so needs a measuring tape)
		// others
		memberDecl := m.toDecl(member.Type)
		memberMt, err := m.createMeasuringTape(memberDecl)
		if err != nil {
			return nil, err
		}
		membersMt = append(membersMt, measuringTapeMember{
			name: string(member.Name),
			mt:   memberMt,
		})
	}
	return &MeasuringTape{
		kind:             Struct,
		name:             fidlcommon.MustReadName(string(decl.Name)),
		decl:             decl,
		inlineNumBytes:   decl.TypeShapeV1.InlineSize,
		members:          membersMt,
		hasHandles:       decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:     decl.TypeShapeV1.Depth > 0,
		inlineNumHandles: decl.TypeShapeV1.MaxHandles,
	}, nil
}

func (m *Measurer) createTableMeasuringTape(decl fidlir.Table) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.SortedMembersNoReserved() {
		memberDecl := m.toDecl(member.Type)
		memberMt, err := m.createMeasuringTape(memberDecl)
		if err != nil {
			return nil, err
		}
		membersMt = append(membersMt, measuringTapeMember{
			name:    string(member.Name),
			ordinal: member.Ordinal,
			mt:      memberMt,
		})
	}
	return &MeasuringTape{
		kind:           Table,
		name:           fidlcommon.MustReadName(string(decl.Name)),
		decl:           decl,
		members:        membersMt,
		hasHandles:     decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:   true,
		inlineNumBytes: 16, // sizeof(fidl_vector_t)
	}, nil
}

func (m *Measurer) createUnionMeasuringTape(decl fidlir.Union) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.Members {
		if member.Reserved {
			continue
		}
		memberDecl := m.toDecl(member.Type)
		memberMt, err := m.createMeasuringTape(memberDecl)
		if err != nil {
			return nil, err
		}
		membersMt = append(membersMt, measuringTapeMember{
			name: string(member.Name),
			mt:   memberMt,
		})
	}
	return &MeasuringTape{
		kind:           Union,
		name:           fidlcommon.MustReadName(string(decl.Name)),
		decl:           decl,
		members:        membersMt,
		isFlexible:     decl.Strictness == fidlir.IsFlexible,
		hasHandles:     decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:   true,
		inlineNumBytes: 24, // sizeof(fidl_xunion_t)
	}, nil
}

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
		return keyedDecl{
			nullable: typ.Nullable,
			decl: vectorDecl{
				elementDecl: m.toDecl(*typ.ElementType),
			},
		}
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
	for _, decl := range root.Protocols {
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
