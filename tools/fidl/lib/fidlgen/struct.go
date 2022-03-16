// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"fmt"
)

func (s Struct) populateFullStructMaskForStruct(mask []byte, flatten bool, getTypeShape func(Struct) TypeShape, getFieldShape func(StructMember) FieldShape, resolveStruct func(identifier EncodedCompoundIdentifier) *Struct) {
	paddingEnd := getTypeShape(s).InlineSize - 1
	for i := len(s.Members) - 1; i >= 0; i-- {
		member := s.Members[i]
		fieldShape := getFieldShape(member)
		if flatten {
			s.populateFullStructMaskForType(mask[fieldShape.Offset:paddingEnd+1], &member.Type, flatten, getTypeShape, getFieldShape, resolveStruct)
		}
		for j := 0; j < fieldShape.Padding; j++ {
			mask[paddingEnd-j] = 0xff
		}
		paddingEnd = fieldShape.Offset - 1
	}
}

func (s Struct) populateFullStructMaskForType(mask []byte, typ *Type, flatten bool, getTypeShape func(Struct) TypeShape, getFieldShape func(StructMember) FieldShape, resolveStruct func(identifier EncodedCompoundIdentifier) *Struct) {
	if typ.Nullable {
		return
	}
	switch typ.Kind {
	case ArrayType:
		elemByteSize := len(mask) / *typ.ElementCount
		for i := 0; i < *typ.ElementCount; i++ {
			s.populateFullStructMaskForType(mask[i*elemByteSize:(i+1)*elemByteSize], typ.ElementType, flatten, getTypeShape, getFieldShape, resolveStruct)
		}
	case IdentifierType:
		sv := resolveStruct(typ.Identifier)
		if sv != nil {
			sv.populateFullStructMaskForStruct(mask, flatten, getTypeShape, getFieldShape, resolveStruct)
		}
	}
}

type PaddingMarker struct {
	// Offset into the struct (0 is the start of the struct).
	Offset int
	// Mask, where a 1-bit means the bit in the input value should be zero.
	Mask []byte
}

func (s Struct) buildPaddingMarkers(flatten bool, getTypeShape func(Struct) TypeShape, getFieldShape func(StructMember) FieldShape, resolveStruct func(identifier EncodedCompoundIdentifier) *Struct) []PaddingMarker {
	var paddingMarkers []PaddingMarker

	// Construct a mask across the whole struct with 0xff bytes where there is padding.
	fullStructMask := make([]byte, getTypeShape(s).InlineSize)
	s.populateFullStructMaskForStruct(fullStructMask, flatten, getTypeShape, getFieldShape, resolveStruct)

	// Split up the mask into aligned integer mask segments that can be outputted in the
	// fidl_struct! macro.
	// Only the sections needing padding are outputted.
	// e.g. 00ffff0000ffff000000000000000000 -> 00ffff0000ffff00, 0000000000000000
	//                                       -> []PaddingMarker{0, []byte{0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00}}
	extractNonzeroSliceOffsets := func(stride int) []int {
		var offsets []int
		for endi := stride - 1; endi < len(fullStructMask); endi += stride {
			i := endi - (stride - 1)
			if bytes.Contains(fullStructMask[i:i+stride], []byte{0xff}) {
				offsets = append(offsets, i)
			}
		}
		return offsets
	}
	zeroSlice := func(s []byte) {
		for i := range s {
			s[i] = 0
		}
	}
	for _, i := range extractNonzeroSliceOffsets(8) {
		s := fullStructMask[i : i+8]
		m := make([]byte, 8)
		copy(m, s)
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Offset: i,
			Mask:   m,
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	for _, i := range extractNonzeroSliceOffsets(4) {
		s := fullStructMask[i : i+4]
		m := make([]byte, 4)
		copy(m, s)
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Offset: i,
			Mask:   m,
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	for _, i := range extractNonzeroSliceOffsets(2) {
		s := fullStructMask[i : i+2]
		m := make([]byte, 2)
		copy(m, s)
		paddingMarkers = append(paddingMarkers, PaddingMarker{
			Offset: i,
			Mask:   m,
		})
		zeroSlice(s) // Reset the buffer for the next iteration.
	}
	if bytes.Contains(fullStructMask, []byte{0xff}) {
		// This shouldn't be possible because it requires an alignment 1 struct to have padding.
		panic(fmt.Sprintf("expected mask to be zero, was %v", fullStructMask))
	}
	return paddingMarkers
}

type WireFormatVersion int

const (
	_ = iota
	WireFormatVersionV1
	WireFormatVersionV2
)

func getTypeShapeFunc(wireFormatVersion WireFormatVersion) func(Struct) TypeShape {
	switch wireFormatVersion {
	case WireFormatVersionV1:
		return func(s Struct) TypeShape {
			return s.TypeShapeV1
		}
	case WireFormatVersionV2:
		return func(s Struct) TypeShape {
			return s.TypeShapeV2
		}
	default:
		panic("unknown wire format version")
	}
}

func getFieldShapeFunc(wireFormatVersion WireFormatVersion) func(StructMember) FieldShape {
	switch wireFormatVersion {
	case WireFormatVersionV1:
		return func(s StructMember) FieldShape {
			return s.FieldShapeV1
		}
	case WireFormatVersionV2:
		return func(s StructMember) FieldShape {
			return s.FieldShapeV2
		}
	default:
		panic("unknown wire format version")
	}
}

func (s Struct) BuildPaddingMarkers(wireFormatVersion WireFormatVersion) []PaddingMarker {
	return s.buildPaddingMarkers(false, getTypeShapeFunc(wireFormatVersion), getFieldShapeFunc(wireFormatVersion), nil)
}

func (s Struct) BuildFlattenedPaddingMarkers(wireFormatVersion WireFormatVersion, resolveStruct func(identifier EncodedCompoundIdentifier) *Struct) []PaddingMarker {
	return s.buildPaddingMarkers(true, getTypeShapeFunc(wireFormatVersion), getFieldShapeFunc(wireFormatVersion), resolveStruct)
}
