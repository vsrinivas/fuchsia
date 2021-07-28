// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
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
		in   fidlgen.Struct
		out  []PaddingMarker
	}
	testCases := []testCase{
		{
			name: "empty",
			in:   fidlgen.Struct{},
			out:  nil,
		},
		{
			name: "no padding 8-bytes",
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 8,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 4,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 2,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 1,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 8,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 4,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 2,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 16,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  4,
							Padding: 2,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 4,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 1,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 16,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 12,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
			in: fidlgen.Struct{
				TypeShapeV1: fidlgen.TypeShape{
					InlineSize: 10,
				},
				Members: []fidlgen.StructMember{
					{
						FieldShapeV1: fidlgen.FieldShape{
							Offset:  0,
							Padding: 0,
						},
					},
					{
						FieldShapeV1: fidlgen.FieldShape{
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
		out := c.buildPaddingMarkers(testCase.in, false, getTypeShapeV1, getFieldShapeV1)
		if diff := cmp.Diff(testCase.out, out); diff != "" {
			t.Errorf("%s:\nexpected != actual (-want +got)\n%s", testCase.name, diff)
		}
	}
}

func TestBuildPaddingMarkersFlatteningStruct(t *testing.T) {
	var innerStructIdentifier fidlgen.EncodedCompoundIdentifier = "abcd"
	innerStruct := fidlgen.Struct{
		TypeShapeV1: fidlgen.TypeShape{
			InlineSize: 4,
		},
		Members: []fidlgen.StructMember{
			{
				FieldShapeV1: fidlgen.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	input := fidlgen.Struct{
		TypeShapeV1: fidlgen.TypeShape{
			InlineSize: 8,
		},
		Members: []fidlgen.StructMember{
			{
				FieldShapeV1: fidlgen.FieldShape{
					Offset:  0,
					Padding: 4,
				},
				Type: fidlgen.Type{
					Kind:       fidlgen.IdentifierType,
					Identifier: innerStructIdentifier,
				},
			},
		},
	}
	resourceness := fidlgen.IsValueType
	c := compiler{
		decls: map[fidlgen.EncodedCompoundIdentifier]fidlgen.DeclInfo{
			innerStructIdentifier: {Type: fidlgen.StructDeclType, Resourceness: &resourceness},
		},
		structs: map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: fidlgen.LibraryIdentifier{""},
	}
	out := c.buildPaddingMarkers(input, true, getTypeShapeV1, getFieldShapeV1)
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
	var innerStructIdentifier fidlgen.EncodedCompoundIdentifier = "abcd"
	innerStruct := fidlgen.Struct{
		TypeShapeV1: fidlgen.TypeShape{
			InlineSize: 4,
		},
		Members: []fidlgen.StructMember{
			{
				FieldShapeV1: fidlgen.FieldShape{
					Offset:  0,
					Padding: 3,
				},
			},
		},
	}
	count := 3
	input := fidlgen.Struct{
		TypeShapeV1: fidlgen.TypeShape{
			InlineSize: 12,
		},
		Members: []fidlgen.StructMember{
			{
				FieldShapeV1: fidlgen.FieldShape{
					Offset:  0,
					Padding: 0,
				},
				Type: fidlgen.Type{
					Kind:         fidlgen.ArrayType,
					ElementCount: &count,
					ElementType: &fidlgen.Type{
						Kind:       fidlgen.IdentifierType,
						Identifier: innerStructIdentifier,
					},
				},
			},
		},
	}
	resourceness := fidlgen.IsValueType
	c := compiler{
		decls: map[fidlgen.EncodedCompoundIdentifier]fidlgen.DeclInfo{
			innerStructIdentifier: {Type: fidlgen.StructDeclType, Resourceness: &resourceness},
		},
		structs: map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct{
			innerStructIdentifier: innerStruct,
		},
		library: fidlgen.LibraryIdentifier{""},
	}
	out := c.buildPaddingMarkers(input, true, getTypeShapeV1, getFieldShapeV1)
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

func TestDerivesCalculation(t *testing.T) {
	cases := []struct {
		fidl     string
		expected string
	}{
		{
			fidl:     `type MyStruct = struct { field string; };`,
			expected: "#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]",
		},
		{
			fidl:     `type MyStruct = struct { field float32; };`,
			expected: "#[derive(Debug, Copy, Clone, PartialEq, PartialOrd)]",
		},
		{
			fidl:     `type MyStruct = resource struct {};`,
			expected: "#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash, zerocopy::AsBytes, zerocopy::FromBytes)]",
		},
	}
	for _, ex := range cases {
		root := Compile(fidlgentest.EndToEndTest{T: t}.Single(`library example; ` + ex.fidl))
		actual := root.Structs[0].Derives.String()
		if ex.expected != actual {
			t.Errorf("%s: expected %s, found %s", ex.fidl, ex.expected, actual)
		}
	}
}
