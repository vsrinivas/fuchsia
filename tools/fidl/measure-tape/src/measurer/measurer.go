// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Measurer struct {
	roots map[fidlgen.LibraryName]fidlgen.Root
	tapes map[string]*MeasuringTape
}

func (m *Measurer) RootLibraries() []fidlgen.LibraryName {
	var libraryNames []fidlgen.LibraryName
	for libraryName := range m.roots {
		libraryNames = append(libraryNames, libraryName)
	}
	return libraryNames
}

func NewMeasurer(roots []fidlgen.Root) *Measurer {
	m := &Measurer{
		roots: make(map[fidlgen.LibraryName]fidlgen.Root),
		tapes: make(map[string]*MeasuringTape),
	}
	for _, root := range roots {
		m.roots[fidlgen.MustReadLibraryName(string(root.Name))] = root
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
	name fidlgen.Name
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

func (mt *MeasuringTape) Name() fidlgen.Name {
	return mt.name
}

type measuringTapeMember struct {
	name    string
	ordinal int
	mt      *MeasuringTape
}

func (m *Measurer) MeasuringTapeFor(targetType string) (*MeasuringTape, error) {
	name, err := fidlgen.ReadName(targetType)
	if err != nil {
		return nil, err
	}
	kd, err := m.lookup(name)
	if err != nil {
		return nil, err
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
	case fidlgen.Struct:
		tape, err = m.createStructMeasuringTape(decl)
	case fidlgen.Table:
		tape, err = m.createTableMeasuringTape(decl)
	case fidlgen.Union:
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
			inlineNumBytes:   toSize(fidlgen.Uint32),
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
		panic(fmt.Sprintf("unexpected decl, was %+v", kd.decl))
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

func (m *Measurer) createStructMeasuringTape(decl fidlgen.Struct) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.Members {
		// all primitives including bits & enums are sized as part of the inline cost
		// string/vector have 8 bytes as part of inline cost, rest is out-of-line (so needs a measuring tape)
		// others
		memberDecl, err := m.toDecl(member.Type)
		if err != nil {
			return nil, err
		}
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
		name:             fidlgen.MustReadName(string(decl.Name)),
		decl:             decl,
		inlineNumBytes:   decl.TypeShapeV1.InlineSize,
		members:          membersMt,
		hasHandles:       decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:     decl.TypeShapeV1.Depth > 0,
		inlineNumHandles: decl.TypeShapeV1.MaxHandles,
	}, nil
}

func (m *Measurer) createTableMeasuringTape(decl fidlgen.Table) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.SortedMembersNoReserved() {
		memberDecl, err := m.toDecl(member.Type)
		if err != nil {
			return nil, err
		}
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
		name:           fidlgen.MustReadName(string(decl.Name)),
		decl:           decl,
		members:        membersMt,
		hasHandles:     decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:   true,
		inlineNumBytes: 16, // sizeof(fidl_vector_t)
	}, nil
}

func (m *Measurer) createUnionMeasuringTape(decl fidlgen.Union) (*MeasuringTape, error) {
	var membersMt []measuringTapeMember
	for _, member := range decl.Members {
		if member.Reserved {
			continue
		}
		memberDecl, err := m.toDecl(member.Type)
		if err != nil {
			return nil, err
		}
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
		name:           fidlgen.MustReadName(string(decl.Name)),
		decl:           decl,
		members:        membersMt,
		isFlexible:     decl.Strictness == fidlgen.IsFlexible,
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

func (m *Measurer) toDecl(typ fidlgen.Type) (keyedDecl, error) {
	switch typ.Kind {
	case fidlgen.ArrayType:
		elementDecl, err := m.toDecl(*typ.ElementType)
		if err != nil {
			return keyedDecl{}, err
		}
		return keyedDecl{
			decl: arrayDecl{
				elementCount: *typ.ElementCount,
				elementDecl:  elementDecl,
			},
		}, nil
	case fidlgen.VectorType:
		elementDecl, err := m.toDecl(*typ.ElementType)
		if err != nil {
			return keyedDecl{}, err
		}
		return keyedDecl{
			nullable: typ.Nullable,
			decl: vectorDecl{
				elementDecl: elementDecl,
			},
		}, nil
	case fidlgen.StringType:
		return keyedDecl{
			nullable: typ.Nullable,
			decl:     stringDecl{},
		}, nil
	case fidlgen.HandleType:
		fallthrough
	case fidlgen.RequestType:
		return keyedDecl{
			decl:     handleDecl{},
			nullable: typ.Nullable,
		}, nil
	case fidlgen.PrimitiveType:
		return keyedDecl{
			decl: primitiveDecl{size: toSize(typ.PrimitiveSubtype)},
		}, nil
	case fidlgen.IdentifierType:
		kd, err := m.lookup(fidlgen.MustReadName(string(typ.Identifier)))
		if err != nil {
			return keyedDecl{}, err
		}
		kd.nullable = typ.Nullable
		return kd, nil
	default:
		panic("unreachable")
	}
}

func (m *Measurer) lookup(name fidlgen.Name) (keyedDecl, error) {
	root, ok := m.roots[name.LibraryName()]
	if !ok {
		return keyedDecl{}, fmt.Errorf("missing definition for %s, you may be missing a JSON IR", name)
	}
	fqn := name.FullyQualifiedName()
	for _, decl := range root.Structs {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, nil
		}
	}
	for _, decl := range root.Tables {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, nil
		}
	}
	for _, decl := range root.Unions {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: decl}, nil
		}
	}
	for _, decl := range root.Enums {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{
				key:  fqn,
				decl: primitiveDecl{size: toSize(decl.Type)},
			}, nil
		}
	}
	for _, decl := range root.Bits {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{
				key:  fqn,
				decl: primitiveDecl{size: toSize(decl.Type.PrimitiveSubtype)},
			}, nil
		}
	}
	for _, decl := range root.Protocols {
		if name := string(decl.Name); name == fqn {
			return keyedDecl{key: fqn, decl: handleDecl{}}, nil
		}
	}
	panic(fmt.Sprintf("unreachable: %s does not refer to a type", name))
}

func toSize(subtype fidlgen.PrimitiveSubtype) int {
	switch subtype {
	case fidlgen.Bool, fidlgen.Int8, fidlgen.Uint8:
		return 1
	case fidlgen.Int16, fidlgen.Uint16:
		return 2
	case fidlgen.Int32, fidlgen.Uint32, fidlgen.Float32:
		return 4
	case fidlgen.Int64, fidlgen.Uint64, fidlgen.Float64:
		return 8
	default:
		panic(fmt.Sprintf("unknown subtype: %v", subtype))
	}
}
