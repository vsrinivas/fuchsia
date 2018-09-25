// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package typestest

import (
	"strconv"

	"fidl/compiler/backend/types"
)

func NumericLiteral(value int) types.Constant {
	return types.Constant{
		Kind: types.LiteralConstant,
		Literal: types.Literal{
			Kind:  types.NumericLiteral,
			Value: strconv.Itoa(value),
		},
	}
}

func BoolLiteral(val bool) types.Constant {
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

func PrimitiveType(kind types.PrimitiveSubtype) types.Type {
	return types.Type{
		Kind:             types.PrimitiveType,
		PrimitiveSubtype: kind,
	}
}

func ArrayType(elementType types.Type, elementCount int) types.Type {
	return types.Type{
		Kind:         types.ArrayType,
		ElementType:  &elementType,
		ElementCount: &elementCount,
	}
}

func VectorType(elementType types.Type, elementCount *int) types.Type {
	return types.Type{
		Kind:         types.VectorType,
		ElementType:  &elementType,
		ElementCount: elementCount,
	}
}

func StringType(elementCount *int) types.Type {
	return types.Type{
		Kind:         types.StringType,
		ElementCount: elementCount,
	}
}

func HandleType() types.Type {
	return types.Type{
		Kind: types.HandleType,
	}
}

func Nullable(t types.Type) types.Type {
	t.Nullable = true
	return t
}
