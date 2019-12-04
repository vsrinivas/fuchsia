// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"

	"fidl/compiler/backend/types"
	. "fidl/compiler/backend/typestest"
)

type expectKind int

const (
	expectEnums expectKind = iota
	expectBits
	expectStructs
	expectUnion
	expectXUnion
	expectInterface
	expectTable
)

func compileExpect(t *testing.T, testName string, kind expectKind, input types.Root, wrapped_expect Root) {
	t.Run(testName, func(t *testing.T) {
		wrapped_actual := Compile(input)
		var actual, expect interface{}
		switch kind {
		case expectEnums:
			actual = wrapped_actual.Enums
			expect = wrapped_expect.Enums
		case expectBits:
			actual = wrapped_actual.Bits
			expect = wrapped_expect.Bits
		case expectStructs:
			actual = wrapped_actual.Structs
			expect = wrapped_expect.Structs
		case expectUnion:
			actual = wrapped_actual.XUnions
			expect = wrapped_expect.XUnions
		case expectXUnion:
			actual = wrapped_actual.XUnions
			expect = wrapped_expect.XUnions
		case expectInterface:
			actual = wrapped_actual.Interfaces
			expect = wrapped_expect.Interfaces
		case expectTable:
			actual = wrapped_actual.Tables
			expect = wrapped_expect.Tables
		default:
			panic(fmt.Sprintf("unknown expect kind %d", kind))
		}
		if diff := cmp.Diff(expect, actual, cmp.AllowUnexported(types.Ordinals{})); diff != "" {
			t.Errorf("expected != actual (-want +got)\n%s", diff)
		}
	})
}

func compileEnumsExpect(t *testing.T, testName string, input []types.Enum, expect []Enum) {
	compileExpect(t, testName, expectEnums, types.Root{Enums: input}, Root{Enums: expect})
}

func compileBitsExpect(t *testing.T, testName string, input []types.Bits, expect []Bits) {
	compileExpect(t, testName, expectBits, types.Root{Bits: input}, Root{Bits: expect})
}

func compileStructsExpect(t *testing.T, testName string, input []types.Struct, expect []Struct) {
	compileExpect(t, testName, expectStructs, types.Root{Structs: input}, Root{Structs: expect})
}

func compileUnionExpect(t *testing.T, testName string, input types.Union, expect XUnion) {
	compileExpect(t, testName, expectUnion, types.Root{Unions: []types.Union{input}}, Root{XUnions: []XUnion{expect}})
}

func compileXUnionExpect(t *testing.T, testName string, input types.XUnion, expect XUnion) {
	compileExpect(t, testName, expectXUnion, types.Root{XUnions: []types.XUnion{input}}, Root{XUnions: []XUnion{expect}})
}

func compileTableExpect(t *testing.T, testName string, input types.Table, expect Table) {
	compileExpect(t, testName, expectTable, types.Root{Tables: []types.Table{input}}, Root{Tables: []Table{expect}})
}

func compileInterfaceExpect(t *testing.T, testName string, input types.Interface, expect Interface) {
	compileExpect(t, testName, expectInterface, types.Root{Interfaces: []types.Interface{input}}, Root{Interfaces: []Interface{expect}})
}

func TestCompileStruct(t *testing.T) {
	t.Parallel()

	compileStructsExpect(t, "Basic struct", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: PrimitiveType(types.Int8),
					Name: types.Identifier("Test"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: PrimitiveType(types.Float32),
					Name: types.Identifier("Test2"),
					FieldShapeOld: types.FieldShape{
						Offset: 4,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "int8",
					PrivateName: "test",
					Name:        "Test",
					FidlTag:     "0",
				},
				{
					Type:        "float32",
					PrivateName: "test2",
					Name:        "Test2",
					FidlTag:     "4",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with name mangling", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("test"),
			Members: []types.StructMember{
				{
					Type: PrimitiveType(types.Int8),
					Name: types.Identifier("test"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "int8",
					PrivateName: "test",
					Name:        "Test",
					FidlTag:     "0",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with array types", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: ArrayType(PrimitiveType(types.Uint8), 10),
					Name: types.Identifier("Flat"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: ArrayType(ArrayType(PrimitiveType(types.Bool), 1), 27),
					Name: types.Identifier("Nested"),
					FieldShapeOld: types.FieldShape{
						Offset: 10,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "[10]uint8",
					PrivateName: "flat",
					Name:        "Flat",
					FidlTag:     "0",
				},
				{
					Type:        "[27][1]bool",
					PrivateName: "nested",
					Name:        "Nested",
					FidlTag:     "10",
				},
			},
		},
	})

	maxElems := 40
	compileStructsExpect(t, "Struct with string types", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: StringType(nil),
					Name: types.Identifier("Flat"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: Nullable(StringType(nil)),
					Name: types.Identifier("Nullable"),
					FieldShapeOld: types.FieldShape{
						Offset: 16,
					},
				},
				{
					Type: Nullable(StringType(&maxElems)),
					Name: types.Identifier("Max"),
					FieldShapeOld: types.FieldShape{
						Offset: 32,
					},
				},
				{
					Type: ArrayType(StringType(nil), 27),
					Name: types.Identifier("Nested"),
					FieldShapeOld: types.FieldShape{
						Offset: 48,
					},
				},
				{
					Type: ArrayType(Nullable(StringType(&maxElems)), 27),
					Name: types.Identifier("NestedNullableMax"),
					FieldShapeOld: types.FieldShape{
						Offset: 480,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "string",
					PrivateName: "flat",
					Name:        "Flat",
					FidlTag:     "0,",
				},
				{
					Type:        "*string",
					PrivateName: "nullable",
					Name:        "Nullable",
					FidlTag:     "16,",
				},
				{
					Type:        "*string",
					PrivateName: "max",
					Name:        "Max",
					FidlTag:     "32,40",
				},
				{
					Type:        "[27]string",
					PrivateName: "nested",
					Name:        "Nested",
					FidlTag:     "48,",
				},
				{
					Type:        "[27]*string",
					PrivateName: "nestedNullableMax",
					Name:        "NestedNullableMax",
					FidlTag:     "480,40",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with vector types", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: VectorType(PrimitiveType(types.Uint8), nil),
					Name: types.Identifier("Flat"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: VectorType(PrimitiveType(types.Uint8), &maxElems),
					Name: types.Identifier("Max"),
					FieldShapeOld: types.FieldShape{
						Offset: 16,
					},
				},
				{
					Type: VectorType(VectorType(PrimitiveType(types.Bool), nil), nil),
					Name: types.Identifier("Nested"),
					FieldShapeOld: types.FieldShape{
						Offset: 32,
					},
				},
				{
					Type: VectorType(Nullable(VectorType(PrimitiveType(types.Bool), nil)), nil),
					Name: types.Identifier("Nullable"),
					FieldShapeOld: types.FieldShape{
						Offset: 48,
					},
				},
				{
					Type: VectorType(VectorType(VectorType(PrimitiveType(types.Uint8), &maxElems), nil), nil),
					Name: types.Identifier("NestedMax"),
					FieldShapeOld: types.FieldShape{
						Offset: 64,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "[]uint8",
					PrivateName: "flat",
					Name:        "Flat",
					FidlTag:     "0,",
				},
				{
					Type:        "[]uint8",
					PrivateName: "max",
					Name:        "Max",
					FidlTag:     "16,40",
				},
				{
					Type:        "[][]bool",
					PrivateName: "nested",
					Name:        "Nested",
					FidlTag:     "32,,",
				},
				{
					Type:        "[]*[]bool",
					PrivateName: "nullable",
					Name:        "Nullable",
					FidlTag:     "48,,",
				},
				{
					Type:        "[][][]uint8",
					PrivateName: "nestedMax",
					Name:        "NestedMax",
					FidlTag:     "64,,,40",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with nullable handles", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: VectorType(Nullable(HandleType()), nil),
					Name: types.Identifier("NullableHandles"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: VectorType(HandleType(), &maxElems),
					Name: types.Identifier("BoundedNonNullableHandles"),
					FieldShapeOld: types.FieldShape{
						Offset: 16,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "[]_zx.Handle",
					PrivateName: "nullableHandles",
					Name:        "NullableHandles",
					FidlTag:     "0,,1",
				},
				{
					Type:        "[]_zx.Handle",
					PrivateName: "boundedNonNullableHandles",
					Name:        "BoundedNonNullableHandles",
					FidlTag:     "16,40,0",
				},
			},
		},
	})

	one, three := 1, 3
	compileStructsExpect(t, "Struct with arrays and vectors", []types.Struct{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Members: []types.StructMember{
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), nil), 2), nil),
					Name: types.Identifier("no_vec_bounds"),
					FieldShapeOld: types.FieldShape{
						Offset: 0,
					},
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), nil), 2), &one),
					Name: types.Identifier("outer_vec_bounds"),
					FieldShapeOld: types.FieldShape{
						Offset: 16,
					},
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), &three), 2), nil),
					Name: types.Identifier("inner_vec_bounds"),
					FieldShapeOld: types.FieldShape{
						Offset: 32,
					},
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), &three), 2), &one),
					Name: types.Identifier("both_vec_bounds"),
					FieldShapeOld: types.FieldShape{
						Offset: 48,
					},
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "noVecBounds",
					Name:        "NoVecBounds",
					FidlTag:     "0,,",
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "outerVecBounds",
					Name:        "OuterVecBounds",
					FidlTag:     "16,1,",
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "innerVecBounds",
					Name:        "InnerVecBounds",
					FidlTag:     "32,,3",
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "bothVecBounds",
					Name:        "BothVecBounds",
					FidlTag:     "48,1,3",
				},
			},
		},
	})
}

func TestCompileEnum(t *testing.T) {
	t.Parallel()

	compileEnumsExpect(t, "Basic enum", []types.Enum{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Type: types.Int64,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("One"),
					Value: NumericLiteral(1),
				},
				{
					Name:  types.Identifier("Two"),
					Value: NumericLiteral(2),
				},
			},
		},
	}, []Enum{
		{
			Name: "Test",
			Type: "int64",
			Members: []EnumMember{
				{
					Name:  "One",
					Value: "1",
				},
				{
					Name:  "Two",
					Value: "2",
				},
			},
		},
	})

	compileEnumsExpect(t, "Bool enum", []types.Enum{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Type: types.Bool,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("One"),
					Value: BoolLiteral(true),
				},
				{
					Name:  types.Identifier("Two"),
					Value: BoolLiteral(false),
				},
			},
		},
	}, []Enum{
		{
			Name: "Test",
			Type: "bool",
			Members: []EnumMember{
				{
					Name:  "One",
					Value: "true",
				},
				{
					Name:  "Two",
					Value: "false",
				},
			},
		},
	})

	compileEnumsExpect(t, "Enum with name mangling", []types.Enum{
		{
			Name: types.EncodedCompoundIdentifier("test"),
			Type: types.Uint32,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("test"),
					Value: NumericLiteral(125412512),
				},
			},
		},
	}, []Enum{
		{
			Name: "Test",
			Type: "uint32",
			Members: []EnumMember{
				{
					Name:  "Test",
					Value: "125412512",
				},
			},
		},
	})
}

func TestCompileBits(t *testing.T) {
	t.Parallel()

	compileBitsExpect(t, "Basic bits", []types.Bits{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Type: PrimitiveType(types.Int64),
			Members: []types.BitsMember{
				{
					Name:  types.Identifier("One"),
					Value: NumericLiteral(1),
				},
				{
					Name:  types.Identifier("Two"),
					Value: NumericLiteral(2),
				},
			},
		},
	}, []Bits{
		{
			Name: "Test",
			Type: "int64",
			Members: []BitsMember{
				{
					Name:  "One",
					Value: "1",
				},
				{
					Name:  "Two",
					Value: "2",
				},
			},
		},
	})

	compileBitsExpect(t, "Bool bits", []types.Bits{
		{
			Name: types.EncodedCompoundIdentifier("Test"),
			Type: PrimitiveType(types.Bool),
			Members: []types.BitsMember{
				{
					Name:  types.Identifier("One"),
					Value: BoolLiteral(true),
				},
				{
					Name:  types.Identifier("Two"),
					Value: BoolLiteral(false),
				},
			},
		},
	}, []Bits{
		{
			Name: "Test",
			Type: "bool",
			Members: []BitsMember{
				{
					Name:  "One",
					Value: "true",
				},
				{
					Name:  "Two",
					Value: "false",
				},
			},
		},
	})

	compileBitsExpect(t, "Bits with name mangling", []types.Bits{
		{
			Name: types.EncodedCompoundIdentifier("test"),
			Type: PrimitiveType(types.Uint32),
			Members: []types.BitsMember{
				{
					Name:  types.Identifier("test"),
					Value: NumericLiteral(125412512),
				},
			},
		},
	}, []Bits{
		{
			Name: "Test",
			Type: "uint32",
			Members: []BitsMember{
				{
					Name:  "Test",
					Value: "125412512",
				},
			},
		},
	})
}

func TestCompileInterface(t *testing.T) {
	compileInterfaceExpect(t, "Basic", types.Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name: types.EncodedCompoundIdentifier("Test"),
		Methods: []types.Method{
			{
				Ordinal:    1,
				GenOrdinal: 1789789,
				Name:       types.Identifier("First"),
				HasRequest: true,
				Request: []types.Parameter{
					{
						Type: PrimitiveType(types.Int16),
						Name: types.Identifier("Value"),
						FieldShapeOld: types.FieldShape{
							Offset: 16,
						},
						FieldShapeV1NoEE: types.FieldShape{
							Offset: 16,
						},
					},
				},
				RequestTypeShapeOld: types.TypeShape{
					InlineSize: 18,
				},
				RequestTypeShapeV1NoEE: types.TypeShape{
					InlineSize: 18,
				},
			},
			{
				Ordinal:    2,
				GenOrdinal: 2789789,
				Name:       types.Identifier("Second"),
				HasRequest: true,
				Request: []types.Parameter{
					{
						Type: Nullable(StringType(nil)),
						Name: types.Identifier("Value"),
						FieldShapeOld: types.FieldShape{
							Offset: 16,
						},
						FieldShapeV1NoEE: types.FieldShape{
							Offset: 16,
						},
					},
				},
				RequestTypeShapeOld: types.TypeShape{
					InlineSize: 32,
				},
				RequestTypeShapeV1NoEE: types.TypeShape{
					InlineSize: 32,
				},
				HasResponse: true,
				Response: []types.Parameter{
					{
						Type: PrimitiveType(types.Uint32),
						Name: types.Identifier("Value"),
						FieldShapeOld: types.FieldShape{
							Offset: 16,
						},
						FieldShapeV1NoEE: types.FieldShape{
							Offset: 16,
						},
					},
				},
				ResponseTypeShapeOld: types.TypeShape{
					InlineSize: 20,
				},
				ResponseTypeShapeV1NoEE: types.TypeShape{
					InlineSize: 20,
				},
			},
		},
	}, Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name:                 "Test",
		ProxyName:            "TestInterface",
		ProxyType:            "ChannelProxy",
		StubName:             "TestStub",
		EventProxyName:       "TestEventProxy",
		TransitionalBaseName: "TestTransitionalBase",
		RequestName:          "TestInterfaceRequest",
		ServerName:           "TestService",
		ServiceNameString:    "",
		ServiceNameConstant:  "TestName",
		Methods: []Method{
			{
				Ordinals: types.NewOrdinalsStep7(
					types.Method{
						Ordinal:    1,
						GenOrdinal: 1789789,
					},
					"TestFirstOrdinal",
					"TestFirstGenOrdinal",
				),
				Name: "First",
				Request: &Struct{
					Name: "testFirstRequest",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "int16",
							FidlTag:     "0",
						},
					},
					InlineSizeOld: 2,
					InlineSizeV1:  2,
				},
				EventExpectName: "ExpectFirst",
				IsEvent:         false,
			},
			{
				Ordinals: types.NewOrdinalsStep7(
					types.Method{
						Ordinal:    2,
						GenOrdinal: 2789789,
					},
					"TestSecondOrdinal",
					"TestSecondGenOrdinal",
				),
				Name: "Second",
				Request: &Struct{
					Name: "testSecondRequest",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "*string",
							FidlTag:     "0,",
						},
					},
					InlineSizeOld: 16,
					InlineSizeV1:  16,
				},
				Response: &Struct{
					Name: "testSecondResponse",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "uint32",
							FidlTag:     "0",
						},
					},
					InlineSizeOld: 4,
					InlineSizeV1:  4,
				},
				EventExpectName: "ExpectSecond",
				IsEvent:         false,
			},
		},
	})

	compileInterfaceExpect(t, "Event and name mangling", types.Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name: types.EncodedCompoundIdentifier("test"),
		Methods: []types.Method{
			{
				Ordinal:     1,
				GenOrdinal:  9,
				Name:        types.Identifier("first"),
				HasResponse: true,
				Response: []types.Parameter{
					{
						Type: StringType(nil),
						Name: types.Identifier("value"),
						FieldShapeOld: types.FieldShape{
							Offset: 16,
						},
						FieldShapeV1NoEE: types.FieldShape{
							Offset: 16,
						},
					},
				},
				ResponseTypeShapeOld: types.TypeShape{
					InlineSize: 32,
				},
				ResponseTypeShapeV1NoEE: types.TypeShape{
					InlineSize: 32,
				},
			},
		},
	}, Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name:                 "Test",
		ProxyName:            "TestInterface",
		ProxyType:            "ChannelProxy",
		StubName:             "TestStub",
		EventProxyName:       "TestEventProxy",
		TransitionalBaseName: "TestTransitionalBase",
		RequestName:          "TestInterfaceRequest",
		ServerName:           "TestService",
		ServiceNameString:    "",
		ServiceNameConstant:  "TestName",
		Methods: []Method{
			{
				Ordinals: types.NewOrdinalsStep7(
					types.Method{
						Ordinal:    1,
						GenOrdinal: 9,
					},
					"TestFirstOrdinal",
					"TestFirstGenOrdinal",
				),
				Name: "First",
				Response: &Struct{
					Name: "testFirstResponse",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "string",
							FidlTag:     "0,",
						},
					},
					InlineSizeOld: 16,
					InlineSizeV1:  16,
				},
				EventExpectName: "ExpectFirst",
				IsEvent:         true,
			},
		},
	})
}

func TestCompileXUnion(t *testing.T) {
	seven := 7
	compileXUnionExpect(
		t,
		"Basic",
		types.XUnion{
			Name: types.EncodedCompoundIdentifier("MyExtensibleUnion"),
			TypeShapeOld: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			TypeShapeV1NoEE: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			Members: []types.XUnionMember{
				{
					Reserved: false,
					Ordinal:  2,
					Name:     "second",
					Type:     VectorType(PrimitiveType(types.Uint32), &seven),
				},
				{
					Reserved: true,
					Ordinal:  11,
				},
			},
		},
		XUnion{
			Name:          "MyExtensibleUnion",
			TagName:       "I_myExtensibleUnionTag",
			InlineSizeOld: 123,
			InlineSizeV1:  123,
			AlignmentOld:  456,
			AlignmentV1:   456,
			Members: []XUnionMember{
				{
					Type:        "[]uint32",
					Ordinal:     2,
					PrivateName: "second",
					Name:        "Second",
					FidlTag:     "2,7",
				},
			},
		})
}

func TestCompileUnion(t *testing.T) {
	seven := 7
	compileUnionExpect(
		t,
		"Basic",
		types.Union{
			Name: types.EncodedCompoundIdentifier("MyUnion"),
			TypeShapeOld: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			TypeShapeV1NoEE: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			Members: []types.UnionMember{
				{
					Reserved:      true,
					XUnionOrdinal: 1,
				},
				{
					Reserved:      false,
					XUnionOrdinal: 2,
					Name:          "second",
					Type:          VectorType(PrimitiveType(types.Uint32), &seven),
				},
			},
		},
		XUnion{
			Name:          "MyUnion",
			TagName:       "I_myUnionTag",
			InlineSizeOld: 24,
			InlineSizeV1:  24,
			AlignmentOld:  8,
			AlignmentV1:   8,
			Members: []XUnionMember{
				{
					Ordinal:     2,
					Type:        "[]uint32",
					Name:        "Second",
					PrivateName: "second",
					FidlTag:     "2,7",
				},
			},
			Strictness: true,
		})
}

func TestCompileTable(t *testing.T) {
	five, seven := 5, 7
	compileTableExpect(
		t,
		"Basic",
		types.Table{
			Name: types.EncodedCompoundIdentifier("Test"),
			TypeShapeOld: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			TypeShapeV1NoEE: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			Members: []types.TableMember{
				{
					Reserved: true,
					Ordinal:  1,
				},
				{
					Ordinal:  2,
					Name:     "second",
					Reserved: false,
					Type:     VectorType(PrimitiveType(types.Uint32), &seven),
				},
			},
		},
		Table{
			Name:          "Test",
			InlineSizeOld: 123,
			InlineSizeV1:  123,
			AlignmentOld:  456,
			AlignmentV1:   456,
			Members: []TableMember{
				{
					Type:              "[]uint32",
					DataField:         "Second",
					PrivateDataField:  "second",
					PresenceField:     "SecondPresent",
					Setter:            "SetSecond",
					Getter:            "GetSecond",
					GetterWithDefault: "GetSecondWithDefault",
					Haser:             "HasSecond",
					Clearer:           "ClearSecond",
					FidlTag:           "2,7",
				},
			},
		})

	compileTableExpect(
		t,
		"MoreComplex",
		types.Table{
			Name: types.EncodedCompoundIdentifier("Test"),
			TypeShapeOld: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			TypeShapeV1NoEE: types.TypeShape{
				InlineSize: 123,
				Alignment:  456,
			},
			Members: []types.TableMember{
				{
					Ordinal:  1,
					Reserved: true,
				},
				{
					Ordinal: 2,
					Name:    "second",
					Type:    VectorType(ArrayType(VectorType(PrimitiveType(types.Uint32), &seven), 6), &five),
				},
				{
					Ordinal:  3,
					Reserved: true,
				},
				{
					Ordinal: 4,
					Name:    "fourth",
					Type:    VectorType(Nullable(HandleType()), &five),
				},
			},
		},
		Table{
			Name:          "Test",
			InlineSizeOld: 123,
			InlineSizeV1:  123,
			AlignmentOld:  456,
			AlignmentV1:   456,
			Members: []TableMember{
				{
					Type:              "[][6][]uint32",
					DataField:         "Second",
					PrivateDataField:  "second",
					PresenceField:     "SecondPresent",
					Setter:            "SetSecond",
					Getter:            "GetSecond",
					GetterWithDefault: "GetSecondWithDefault",
					Haser:             "HasSecond",
					Clearer:           "ClearSecond",
					FidlTag:           "2,5,7",
				},
				{
					Type:              "[]_zx.Handle",
					DataField:         "Fourth",
					PrivateDataField:  "fourth",
					PresenceField:     "FourthPresent",
					Setter:            "SetFourth",
					Getter:            "GetFourth",
					GetterWithDefault: "GetFourthWithDefault",
					Haser:             "HasFourth",
					Clearer:           "ClearFourth",
					FidlTag:           "4,5,1",
				},
			},
		})
}
