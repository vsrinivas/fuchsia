// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"testing"

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
