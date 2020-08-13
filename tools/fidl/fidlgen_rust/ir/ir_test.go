// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"testing"

	"fidl/compiler/backend/types"

	"github.com/google/go-cmp/cmp"
)

func TestBuildPaddingMarkersWithoutFlattening(t *testing.T) {
	type testCase struct {
		name string
		in   types.Struct
		out  []PaddingMarker
	}
	testCases := []testCase{
		{
			name: "empty",
			in:   types.Struct{},
			out:  nil,
		},
		{
			name: "no padding 8-bytes",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 8,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  4,
							Padding: 0,
						},
					},
				},
			},
			out: nil,
		},
		{
			name: "no padding 4-bytes",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 4,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  2,
							Padding: 0,
						},
					},
				},
			},
			out: nil,
		},
		{
			name: "no padding 2-bytes",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 2,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  1,
							Padding: 0,
						},
					},
				},
			},
			out: nil,
		},
		{
			name: "no padding 1-byte",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 1,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
				},
			},
			out: nil,
		},
		{
			name: "8-byte struct with 2 bytes of padding at end",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 8,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  4,
							Padding: 2,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u64",
					Offset: 0,
					Mask:   "0xffff000000000000u64",
				},
			},
		},
		{
			name: "4-byte struct with 1 byte of padding at end",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 4,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  2,
							Padding: 1,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u32",
					Offset: 0,
					Mask:   "0xff000000u32",
				},
			},
		},
		{
			name: "2-byte struct with 1 byte padding at end",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 2,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 1,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u16",
					Offset: 0,
					Mask:   "0xff00u16",
				},
			},
		},
		{
			name: "padding at end of 8-byte chunk, before next chunk",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 16,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  4,
							Padding: 2,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  8,
							Padding: 0,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u64",
					Offset: 0,
					Mask:   "0xffff000000000000u64",
				},
			},
		},
		{
			name: "padding in middle of 4-byte block",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 4,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 1,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  2,
							Padding: 0,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u32",
					Offset: 0,
					Mask:   "0x0000ff00u32",
				},
			},
		},
		{
			name: "8 byte mask with non-zero offset",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 16,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  8,
							Padding: 4,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u64",
					Offset: 8,
					Mask:   "0xffffffff00000000u64",
				},
			},
		},
		{
			name: "4 byte mask with non-zero offset",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 12,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  8,
							Padding: 2,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u32",
					Offset: 8,
					Mask:   "0xffff0000u32",
				},
			},
		},
		{
			name: "2 byte mask with non-zero offset",
			in: types.Struct{
				TypeShapeV1: types.TypeShape{
					InlineSize: 10,
				},
				Members: []types.StructMember{
					{
						FieldShapeV1: types.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: types.FieldShape{
							Offset:  8,
							Padding: 1,
						},
					},
				},
			},
			out: []PaddingMarker{
				{
					Type:   "u16",
					Offset: 8,
					Mask:   "0xff00u16",
				},
			},
		},
	}
	for _, testCase := range testCases {
		c := compiler{}
		out := c.buildPaddingMarkers(testCase.in, false)
		if diff := cmp.Diff(testCase.out, out); diff != "" {
			t.Errorf("%s:\nexpected != actual (-want +got)\n%s", testCase.name, diff)
		}
	}
}

func TestBuildPaddingMarkersFlatteningStruct(t *testing.T) {
	var innerStructIdentifier types.EncodedCompoundIdentifier = "abcd"
	innerStruct := types.Struct{
		TypeShapeV1: types.TypeShape{
			InlineSize: 4,
		},
		Members: []types.StructMember{
			{
				FieldShapeV1: types.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	input := types.Struct{
		TypeShapeV1: types.TypeShape{
			InlineSize: 8,
		},
		Members: []types.StructMember{
			{
				FieldShapeV1: types.FieldShape{
					Offset:  0,
					Padding: 4,
				},
				Type: types.Type{
					Kind:       types.IdentifierType,
					Identifier: innerStructIdentifier,
				},
			},
		},
	}
	c := compiler{
		decls: map[types.EncodedCompoundIdentifier]types.DeclType{
			innerStructIdentifier: types.StructDeclType,
		},
		structs: map[types.EncodedCompoundIdentifier]types.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: types.LibraryIdentifier{""},
	}
	out := c.buildPaddingMarkers(input, true)
	expected := []PaddingMarker{
		{
			Type:   "u64",
			Offset: 0,
			Mask:   "0xffffffffffffff00u64",
		},
	}
	if diff := cmp.Diff(expected, out); diff != "" {
		t.Errorf("expected != actual (-want +got)\n%s", diff)
	}
}

func TestBuildPaddingMarkersFlatteningArray(t *testing.T) {
	var innerStructIdentifier types.EncodedCompoundIdentifier = "abcd"
	innerStruct := types.Struct{
		TypeShapeV1: types.TypeShape{
			InlineSize: 4,
		},
		Members: []types.StructMember{
			{
				FieldShapeV1: types.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	count := 3
	input := types.Struct{
		TypeShapeV1: types.TypeShape{
			InlineSize: 12,
		},
		Members: []types.StructMember{
			{
				FieldShapeV1: types.FieldShape{
					Offset:  0,
					Padding: 0,
				},
				Type: types.Type{
					Kind:         types.ArrayType,
					ElementCount: &count,
					ElementType: &types.Type{
						Kind:       types.IdentifierType,
						Identifier: innerStructIdentifier,
					},
				},
			},
		},
	}
	c := compiler{
		decls: map[types.EncodedCompoundIdentifier]types.DeclType{
			innerStructIdentifier: types.StructDeclType,
		},
		structs: map[types.EncodedCompoundIdentifier]types.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: types.LibraryIdentifier{""},
	}
	out := c.buildPaddingMarkers(input, true)
	expected := []PaddingMarker{
		{
			Type:   "u64",
			Offset: 0,
			Mask:   "0xffffff00ffffff00u64",
		},
		{
			Type:   "u32",
			Offset: 8,
			Mask:   "0xffffff00u32",
		},
	}
	if diff := cmp.Diff(expected, out); diff != "" {
		t.Errorf("expected != actual (-want +got)\n%s", diff)
	}
}
