// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"reflect"
	"testing"

	"fidl/compiler/backend/types"
)

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

func TestCompileStruct(t *testing.T) {
	t.Parallel()

	compileExpect(t, "Basic struct", types.Root{
		Structs: []types.Struct{
			{
				Name: types.Identifier("Test"),
				Members: []types.StructMember{
					{
						Type: primitiveType("int8"),
						Name: types.Identifier("Test"),
					},
					{
						Type: primitiveType("float32"),
						Name: types.Identifier("Test2"),
					},
				},
			},
		},
	}, Root{
		Structs: []Struct{
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
		},
	})

	compileExpect(t, "Struct with name mangling", types.Root{
		Structs: []types.Struct{
			{
				Name: types.Identifier("test"),
				Members: []types.StructMember{
					{
						Type: primitiveType("int8"),
						Name: types.Identifier("test"),
					},
				},
			},
		},
	}, Root{
		Structs: []Struct{
			{
				Name: "Test",
				Members: []StructMember{
					{
						Type: "int8",
						Name: "Test",
					},
				},
			},
		},
	})
}
