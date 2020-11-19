// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func TestDerivesToString(t *testing.T) {
	cases := []struct {
		input    derives
		expected string
	}{
		{0, ""},
		{derivesDebug, "#[derive(Debug)]"},
		{derivesPartialOrd, "#[derive(PartialOrd)]"},
		{derivesHash | derivesAsBytes, "#[derive(Hash, zerocopy::AsBytes)]"},
	}
	for _, ex := range cases {
		actual := ex.input.String()
		if actual != ex.expected {
			t.Errorf("%d: expected '%s', actual '%s'", ex.input, ex.expected, actual)
		}
	}
}

func TestBuildPaddingMarkersWithoutFlattening(t *testing.T) {
	type testCase struct {
		name string
		in   fidl.Struct
		out  []PaddingMarker
	}
	testCases := []testCase{
		{
			name: "empty",
			in:   fidl.Struct{},
			out:  nil,
		},
		{
			name: "no padding 8-bytes",
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 8,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 4,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 2,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 1,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 8,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 4,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 2,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 16,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  4,
							Padding: 2,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 4,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 1,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 16,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 12,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
			in: fidl.Struct{
				TypeShapeV1: fidl.TypeShape{
					InlineSize: 10,
				},
				Members: []fidl.StructMember{
					{
						FieldShapeV1: fidl.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidl.FieldShape{
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
	var innerStructIdentifier fidl.EncodedCompoundIdentifier = "abcd"
	innerStruct := fidl.Struct{
		TypeShapeV1: fidl.TypeShape{
			InlineSize: 4,
		},
		Members: []fidl.StructMember{
			{
				FieldShapeV1: fidl.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	input := fidl.Struct{
		TypeShapeV1: fidl.TypeShape{
			InlineSize: 8,
		},
		Members: []fidl.StructMember{
			{
				FieldShapeV1: fidl.FieldShape{
					Offset:  0,
					Padding: 4,
				},
				Type: fidl.Type{
					Kind:       fidl.IdentifierType,
					Identifier: innerStructIdentifier,
				},
			},
		},
	}
	resourceness := fidl.IsValueType
	c := compiler{
		decls: map[fidl.EncodedCompoundIdentifier]fidl.DeclInfo{
			innerStructIdentifier: {Type: fidl.StructDeclType, Resourceness: &resourceness},
		},
		structs: map[fidl.EncodedCompoundIdentifier]fidl.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: fidl.LibraryIdentifier{""},
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
	var innerStructIdentifier fidl.EncodedCompoundIdentifier = "abcd"
	innerStruct := fidl.Struct{
		TypeShapeV1: fidl.TypeShape{
			InlineSize: 4,
		},
		Members: []fidl.StructMember{
			{
				FieldShapeV1: fidl.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	count := 3
	input := fidl.Struct{
		TypeShapeV1: fidl.TypeShape{
			InlineSize: 12,
		},
		Members: []fidl.StructMember{
			{
				FieldShapeV1: fidl.FieldShape{
					Offset:  0,
					Padding: 0,
				},
				Type: fidl.Type{
					Kind:         fidl.ArrayType,
					ElementCount: &count,
					ElementType: &fidl.Type{
						Kind:       fidl.IdentifierType,
						Identifier: innerStructIdentifier,
					},
				},
			},
		},
	}
	resourceness := fidl.IsValueType
	c := compiler{
		decls: map[fidl.EncodedCompoundIdentifier]fidl.DeclInfo{
			innerStructIdentifier: {Type: fidl.StructDeclType, Resourceness: &resourceness},
		},
		structs: map[fidl.EncodedCompoundIdentifier]fidl.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: fidl.LibraryIdentifier{""},
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
