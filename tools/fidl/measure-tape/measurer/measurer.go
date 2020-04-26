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

type Measurer struct {
	roots map[fidlcommon.LibraryName]fidlir.Root
	tapes map[string]*MeasuringTape
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

type tapeKind int

const (
	_ tapeKind = iota
	kStruct
	kUnion
	kTable
	kVector
	kString
	kArray
	kHandle
	kPrimitive
)

type MeasuringTape struct {
	kind tapeKind

	// name stores the name of the declaration if relevant, i.e. kUnion, kStruct, kTable
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

	// elementCount for arrays
	elementCount int

	// elementMt for arrays
	elementMt *MeasuringTape
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
			kind:           kPrimitive,
			inlineNumBytes: decl.size,
		}
	case handleDecl:
		tape = &MeasuringTape{
			kind:             kHandle,
			hasHandles:       true,
			inlineNumBytes:   toSize(fidlir.Uint32),
			inlineNumHandles: 1,
		}
	case stringDecl:
		tape = &MeasuringTape{
			kind:           kString,
			inlineNumBytes: 16, // sizeof(fidl_string_t)
			hasOutOfLine:   true,
		}
	case vectorDecl:
		elementMt, err := m.createMeasuringTape(decl.elementDecl)
		if err != nil {
			return nil, err
		}
		tape = &MeasuringTape{
			kind:           kVector,
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
			kind:             kArray,
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
		kind:             kStruct,
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
		kind:           kTable,
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
		kind:           kUnion,
		name:           fidlcommon.MustReadName(string(decl.Name)),
		decl:           decl,
		members:        membersMt,
		hasHandles:     decl.TypeShapeV1.MaxHandles != 0,
		hasOutOfLine:   true,
		inlineNumBytes: 24, // sizeof(fidl_xunion_t)
	}, nil
}
