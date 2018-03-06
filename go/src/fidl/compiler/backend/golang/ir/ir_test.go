// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"reflect"
	"strconv"
	"testing"

	"fidl/compiler/backend/types"
)

func numericLiteral(value int) types.Constant {
	return types.Constant{
		Kind: types.LiteralConstant,
		Literal: types.Literal{
			Kind:  types.NumericLiteral,
			Value: strconv.Itoa(value),
		},
	}
}

func boolLiteral(val bool) types.Constant {
	var kind types.LiteralKind
	if val {
		kind = types.TrueLiteral
	} else {
		kind = types.FalseLiteral
	}
	return types.Constant{
		Kind: types.LiteralConstant,
		Literal: types.Literal{
			Kind: kind,
		},
	}
}

func primitiveType(kind types.PrimitiveSubtype) types.Type {
	return types.Type{
		Kind:             types.PrimitiveType,
		PrimitiveSubtype: kind,
	}
}

func arrayType(elementType types.Type, elementCount int) types.Type {
	return types.Type{
		Kind:         types.ArrayType,
		ElementType:  &elementType,
		ElementCount: &elementCount,
	}
}

func stringType(elementCount *int) types.Type {
	return types.Type{
		Kind: types.StringType,
		ElementCount: elementCount,
	}
}

func nullable(t types.Type) types.Type {
	t.Nullable = true
	return t
}

func compileExpect(t *testing.T, testName string, input types.Root, expect Root) {
	t.Run(testName, func(t *testing.T) {
		actual := Compile(input)
		if !reflect.DeepEqual(actual, expect) {
			t.Fatalf("expected: %v, got %v", expect, actual)
		}
	})
}

func compileEnumsExpect(t *testing.T, testName string, input []types.Enum, expect []Enum) {
	compileExpect(t, testName, types.Root{Enums: input}, Root{Enums: expect})
}

func compileStructsExpect(t *testing.T, testName string, input []types.Struct, expect []Struct) {
	compileExpect(t, testName, types.Root{Structs: input}, Root{Structs: expect})
}

func TestCompileStruct(t *testing.T) {
	t.Parallel()

	compileStructsExpect(t, "Basic struct", []types.Struct{
		{
			Name: types.Identifier("Test"),
			Members: []types.StructMember{
				{
					Type: primitiveType(types.Int8),
					Name: types.Identifier("Test"),
				},
				{
					Type: primitiveType(types.Float32),
					Name: types.Identifier("Test2"),
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type: "int8",
					Name: "Test",
				},
				{
					Type: "float32",
					Name: "Test2",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with name mangling", []types.Struct{
		{
			Name: types.Identifier("test"),
			Members: []types.StructMember{
				{
					Type: primitiveType(types.Int8),
					Name: types.Identifier("test"),
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type: "int8",
					Name: "Test",
				},
			},
		},
	})

	compileStructsExpect(t, "Struct with array types", []types.Struct{
		{
			Name: types.Identifier("Test"),
			Members: []types.StructMember{
				{
					Type: arrayType(primitiveType(types.Uint8), 10),
					Name: types.Identifier("Flat"),
				},
				{
					Type: arrayType(arrayType(primitiveType(types.Bool), 1), 27),
					Name: types.Identifier("Nested"),
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type: "[10]uint8",
					Name: "Flat",
				},
				{
					Type: "[27][1]bool",
					Name: "Nested",
				},
			},
		},
	})

	maxElems := 40
	compileStructsExpect(t, "Struct with string types", []types.Struct{
		{
			Name: types.Identifier("Test"),
			Members: []types.StructMember{
				{
					Type: stringType(nil),
					Name: types.Identifier("Flat"),
				},
				{
					Type: nullable(stringType(nil)),
					Name: types.Identifier("Nullable"),
				},
				{
					Type: nullable(stringType(&maxElems)),
					Name: types.Identifier("Max"),
				},
				{
					Type: arrayType(stringType(nil), 27),
					Name: types.Identifier("Nested"),
				},
				{
					Type: arrayType(nullable(stringType(&maxElems)), 27),
					Name: types.Identifier("NestedNullableMax"),
				},
			},
		},
	}, []Struct{
		{
			Name: "Test",
			Members: []StructMember{
				{
					Type: "string",
					Name: "Flat",
				},
				{
					Type: "*string",
					Name: "Nullable",
				},
				{
					Type: "*string",
					Name: "Max",
					Tag: "`fidl:\"40\"`", 
				},
				{
					Type: "[27]string",
					Name: "Nested",
				},
				{
					Type: "[27]*string",
					Name: "NestedNullableMax",
					Tag: "`fidl:\"40\"`", 
				},
			},
		},
	})
}

func TestCompileEnum(t *testing.T) {
	t.Parallel()

	compileEnumsExpect(t, "Basic enum", []types.Enum{
		{
			Name: types.Identifier("Test"),
			Type: types.Int64,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("One"),
					Value: numericLiteral(1),
				},
				{
					Name:  types.Identifier("Two"),
					Value: numericLiteral(2),
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
			Name: types.Identifier("Test"),
			Type: types.Bool,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("One"),
					Value: boolLiteral(true),
				},
				{
					Name:  types.Identifier("Two"),
					Value: boolLiteral(false),
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
			Name: types.Identifier("test"),
			Type: types.Uint32,
			Members: []types.EnumMember{
				{
					Name:  types.Identifier("test"),
					Value: numericLiteral(125412512),
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
