// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"fidl/compiler/backend/types"
)

func makeLiteralConstant(value string) types.Constant {
	return types.Constant{
		Kind: types.LiteralConstant,
		Literal: types.Literal{
			Kind:  types.NumericLiteral,
			Value: value,
		},
	}
}

func makePrimitiveType(subtype types.PrimitiveSubtype) types.Type {
	return types.Type{
		Kind:             types.PrimitiveType,
		PrimitiveSubtype: subtype,
	}
}

func TestCompileXUnion(t *testing.T) {
	cases := []struct {
		name     string
		input    types.XUnion
		expected XUnion
	}{
		{
			name: "SingleInt64",
			input: types.XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.XUnionMember{
					{
						Reserved: true,
						Ordinal:  1,
					},
					{
						Reserved:   false,
						Attributes: types.Attributes{},
						Ordinal:    2,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name:         types.Identifier("i"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
				Strictness:   types.IsFlexible,
			},
			expected: XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Derives: derivesDebug | derivesPartialEq,
				ECI:     types.EncodedCompoundIdentifier("Test"),
				Name:    "Test",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						Ordinal:    2,
						Type:       "i64",
						OGType: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name: "I",
					},
				},
				Strictness: types.IsFlexible,
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				XUnions: []types.XUnion{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
				Decls: map[types.EncodedCompoundIdentifier]types.DeclType{
					ex.input.Name: types.XUnionDeclType,
				},
			}
			result := Compile(root)
			actual := result.XUnions[0]

			if diff := cmp.Diff(ex.expected, actual); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func TestCompileUnion(t *testing.T) {
	// Note: At this point in the union-to-xunion migration, JSON IR unions are
	// compiled into static xunions in the Rust fidlgen IR.
	cases := []struct {
		name     string
		input    types.Union
		expected XUnion
	}{
		{
			name: "SingleInt64",
			input: types.Union{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				Name: types.EncodedCompoundIdentifier("Test"),
				Members: []types.UnionMember{
					{
						Reserved:      true,
						XUnionOrdinal: 1,
					},
					{
						Reserved:      false,
						Attributes:    types.Attributes{},
						XUnionOrdinal: 2,
						Type: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Name:         types.Identifier("i"),
						Offset:       0,
						MaxOutOfLine: 0,
					},
				},
				Size:         24,
				MaxHandles:   0,
				MaxOutOfLine: 4294967295,
			},
			expected: XUnion{
				Attributes: types.Attributes{
					Attributes: []types.Attribute{
						{
							Name:  types.Identifier("Foo"),
							Value: "Bar",
						},
					},
				},
				ECI:     types.EncodedCompoundIdentifier("Test"),
				Derives: derivesAll,
				Name:    "Test",
				Members: []XUnionMember{
					{
						Attributes: types.Attributes{},
						OGType: types.Type{
							Kind:             types.PrimitiveType,
							PrimitiveSubtype: types.Int64,
						},
						Type:    "i64",
						Name:    "I",
						Ordinal: 2,
					},
				},
				Strictness: types.IsStrict,
			},
		},
	}
	for _, ex := range cases {
		t.Run(ex.name, func(t *testing.T) {
			root := types.Root{
				Unions: []types.Union{ex.input},
				DeclOrder: []types.EncodedCompoundIdentifier{
					ex.input.Name,
				},
				Decls: map[types.EncodedCompoundIdentifier]types.DeclType{
					ex.input.Name: types.UnionDeclType,
				},
			}
			result := Compile(root)
			actual := result.XUnions[0]

			if diff := cmp.Diff(ex.expected, actual); diff != "" {
				t.Errorf("expected != actual (-want +got)\n%s", diff)
			}
		})
	}
}

func TestCompileConstant(t *testing.T) {
	var c compiler
	cases := []struct {
		input    types.Constant
		typ      types.Type
		expected string
	}{
		{
			input:    makeLiteralConstant("10"),
			typ:      makePrimitiveType(types.Uint32),
			expected: "10",
		},
		{
			input:    makeLiteralConstant("10"),
			typ:      makePrimitiveType(types.Float32),
			expected: "10.0",
		},
		{
			input:    makeLiteralConstant("-1"),
			typ:      makePrimitiveType(types.Int16),
			expected: "-1",
		},
		{
			input:    makeLiteralConstant("0xA"),
			typ:      makePrimitiveType(types.Uint32),
			expected: "0xA",
		},
		{
			input:    makeLiteralConstant("1.23"),
			typ:      makePrimitiveType(types.Float32),
			expected: "1.23",
		},
	}
	for _, ex := range cases {
		actual := c.compileConstant(ex.input, ex.typ)
		if ex.expected != actual {
			t.Errorf("%v: expected %s, actual %s", ex.input, ex.expected, actual)
		}
	}
}
