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
			Kind: types.NumericLiteral,
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
		Kind: types.PrimitiveType,
		PrimitiveSubtype: kind,
	}
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
}

func TestCompileEnum(t *testing.T) {
	t.Parallel()

	compileEnumsExpect(t, "Basic enum", []types.Enum{
		{
			Name: types.Identifier("Test"),
			Type: types.Int64,
			Members: []types.EnumMember{
				{
					Name: types.Identifier("One"),
					Value: numericLiteral(1),
				},
				{
					Name: types.Identifier("Two"),
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
					Name: "One",
					Value: "1",
				},
				{
					Name: "Two",
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
					Name: types.Identifier("One"),
					Value: boolLiteral(true),
				},
				{
					Name: types.Identifier("Two"),
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
					Name: "One",
					Value: "true",
				},
				{
					Name: "Two",
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
					Name: types.Identifier("test"),
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
					Name: "Test",
					Value: "125412512",
				},
			},
		},
	})
}
