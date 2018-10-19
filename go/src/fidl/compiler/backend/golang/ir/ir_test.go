// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fmt"
	"reflect"
	"testing"

	"fidl/compiler/backend/types"
	. "fidl/compiler/backend/typestest"
)

type expectKind int

const (
	expectEnum expectKind = iota
	expectStructs
	expectInterface
)

func compileExpect(t *testing.T, testName string, kind expectKind, input types.Root, wrapped_expect Root) {
	t.Run(testName, func(t *testing.T) {
		wrapped_actual := Compile(input)
		var actual, expect interface{}
		switch kind {
		case expectEnum:
			actual = wrapped_actual.Enums
			expect = wrapped_expect.Enums
		case expectStructs:
			actual = wrapped_actual.Structs
			expect = wrapped_expect.Structs
		case expectInterface:
			actual = wrapped_actual.Interfaces
			expect = wrapped_expect.Interfaces
		default:
			panic(fmt.Sprintf("unknown expect kind %d", kind))
		}
		if !reflect.DeepEqual(actual, expect) {
			t.Fatalf("expected: %+v, got %+v", expect, actual)
		}
	})
}

func compileEnumsExpect(t *testing.T, testName string, input []types.Enum, expect []Enum) {
	compileExpect(t, testName, expectEnum, types.Root{Enums: input}, Root{Enums: expect})
}

func compileStructsExpect(t *testing.T, testName string, input []types.Struct, expect []Struct) {
	compileExpect(t, testName, expectStructs, types.Root{Structs: input}, Root{Structs: expect})
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
				},
				{
					Type: PrimitiveType(types.Float32),
					Name: types.Identifier("Test2"),
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
				},
				{
					Type:        "float32",
					PrivateName: "test2",
					Name:        "Test2",
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
				},
				{
					Type: ArrayType(ArrayType(PrimitiveType(types.Bool), 1), 27),
					Name: types.Identifier("Nested"),
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
				},
				{
					Type:        "[27][1]bool",
					PrivateName: "nested",
					Name:        "Nested",
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
				},
				{
					Type: Nullable(StringType(nil)),
					Name: types.Identifier("Nullable"),
				},
				{
					Type: Nullable(StringType(&maxElems)),
					Name: types.Identifier("Max"),
				},
				{
					Type: ArrayType(StringType(nil), 27),
					Name: types.Identifier("Nested"),
				},
				{
					Type: ArrayType(Nullable(StringType(&maxElems)), 27),
					Name: types.Identifier("NestedNullableMax"),
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
				},
				{
					Type:        "*string",
					PrivateName: "nullable",
					Name:        "Nullable",
				},
				{
					Type:        "*string",
					PrivateName: "max",
					Name:        "Max",
					Tags:        "`" + `fidl:"40" fidl2:"40"` + "`",
				},
				{
					Type:        "[27]string",
					PrivateName: "nested",
					Name:        "Nested",
				},
				{
					Type:        "[27]*string",
					PrivateName: "nestedNullableMax",
					Name:        "NestedNullableMax",
					Tags:        "`" + `fidl:"40" fidl2:"40"` + "`",
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
				},
				{
					Type: VectorType(PrimitiveType(types.Uint8), &maxElems),
					Name: types.Identifier("Max"),
				},
				{
					Type: VectorType(VectorType(PrimitiveType(types.Bool), nil), nil),
					Name: types.Identifier("Nested"),
				},
				{
					Type: VectorType(Nullable(VectorType(PrimitiveType(types.Bool), nil)), nil),
					Name: types.Identifier("Nullable"),
				},
				{
					Type: VectorType(VectorType(VectorType(PrimitiveType(types.Uint8), &maxElems), nil), nil),
					Name: types.Identifier("NestedMax"),
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
				},
				{
					Type:        "[]uint8",
					PrivateName: "max",
					Name:        "Max",
					Tags:        "`" + `fidl:"40" fidl2:"40"` + "`",
				},
				{
					Type:        "[][]bool",
					PrivateName: "nested",
					Name:        "Nested",
				},
				{
					Type:        "[]*[]bool",
					PrivateName: "nullable",
					Name:        "Nullable",
				},
				{
					Type:        "[][][]uint8",
					PrivateName: "nestedMax",
					Name:        "NestedMax",
					Tags:        "`" + `fidl:"40,," fidl2:",,40"` + "`",
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
				},
				{
					Type: VectorType(HandleType(), &maxElems),
					Name: types.Identifier("BoundedNonNullableHandles"),
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
					Tags:        "`" + `fidl:"*," fidl2:",1"` + "`",
				},
				{
					Type:        "[]_zx.Handle",
					PrivateName: "boundedNonNullableHandles",
					Name:        "BoundedNonNullableHandles",
					Tags:        "`" + `fidl:"40" fidl2:"40,0"` + "`",
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
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), nil), 2), &one),
					Name: types.Identifier("outer_vec_bounds"),
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), &three), 2), nil),
					Name: types.Identifier("inner_vec_bounds"),
				},
				{
					Type: VectorType(ArrayType(VectorType(ArrayType(PrimitiveType(types.Uint8), 4), &three), 2), &one),
					Name: types.Identifier("both_vec_bounds"),
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
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "outerVecBounds",
					Name:        "OuterVecBounds",
					Tags:        "`" + `fidl:",1" fidl2:"1,"` + "`",
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "innerVecBounds",
					Name:        "InnerVecBounds",
					Tags:        "`" + `fidl:"3," fidl2:",3"` + "`",
				},
				{
					Type:        "[][2][][4]uint8",
					PrivateName: "bothVecBounds",
					Name:        "BothVecBounds",
					Tags:        "`" + `fidl:"3,1" fidl2:"1,3"` + "`",
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
				Ordinal:    types.Ordinal(1),
				Name:       types.Identifier("First"),
				HasRequest: true,
				Request: []types.Parameter{
					{
						Type: PrimitiveType(types.Int16),
						Name: types.Identifier("Value"),
					},
				},
				RequestSize: 18,
			},
			{
				Ordinal:    types.Ordinal(2),
				Name:       types.Identifier("Second"),
				HasRequest: true,
				Request: []types.Parameter{
					{
						Type: Nullable(StringType(nil)),
						Name: types.Identifier("Value"),
					},
				},
				RequestSize: 32,
				HasResponse: true,
				Response: []types.Parameter{
					{
						Type: PrimitiveType(types.Uint32),
						Name: types.Identifier("Value"),
					},
				},
				ResponseSize: 20,
			},
		},
	}, Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name:                "Test",
		ProxyName:           "TestInterface",
		StubName:            "TestStub",
		EventProxyName:      "TestEventProxy",
		RequestName:         "TestInterfaceRequest",
		ServerName:          "TestService",
		ServiceNameString:   "",
		ServiceNameConstant: "TestName",
		Methods: []Method{
			{
				Ordinal:     types.Ordinal(1),
				OrdinalName: "TestFirstOrdinal",
				Name:        "First",
				Request: &Struct{
					Name: "TestFirstRequest",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "int16",
						},
					},
					Size: 2,
				},
				EventExpectName: "ExpectFirst",
				IsEvent:         false,
			},
			{
				Ordinal:     types.Ordinal(2),
				OrdinalName: "TestSecondOrdinal",
				Name:        "Second",
				Request: &Struct{
					Name: "TestSecondRequest",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "*string",
						},
					},
					Size: 16,
				},
				Response: &Struct{
					Name: "TestSecondResponse",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "uint32",
						},
					},
					Size: 4,
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
				Ordinal:     types.Ordinal(1),
				Name:        types.Identifier("first"),
				HasResponse: true,
				Response: []types.Parameter{
					{
						Type: StringType(nil),
						Name: types.Identifier("value"),
					},
				},
				ResponseSize: 32,
			},
		},
	}, Interface{
		Attributes: types.Attributes{
			Attributes: []types.Attribute{
				{Name: types.Identifier("ServiceName"), Value: "Test"},
			},
		},
		Name:                "Test",
		ProxyName:           "TestInterface",
		StubName:            "TestStub",
		EventProxyName:      "TestEventProxy",
		RequestName:         "TestInterfaceRequest",
		ServerName:          "TestService",
		ServiceNameString:   "",
		ServiceNameConstant: "TestName",
		Methods: []Method{
			{
				Ordinal:     types.Ordinal(1),
				OrdinalName: "TestFirstOrdinal",
				Name:        "First",
				Response: &Struct{
					Name: "TestFirstResponse",
					Members: []StructMember{
						{
							Name:        "Value",
							PrivateName: "value",
							Type:        "string",
						},
					},
					Size: 16,
				},
				EventExpectName: "ExpectFirst",
				IsEvent:         true,
			},
		},
	})
}
