// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	//"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

func TestTranslateLiteralValue(t *testing.T) {
	testCases := []struct {
		expected     string
		literalValue mojom_types.LiteralValue
	}{
		{"true", &mojom_types.LiteralValueBoolValue{true}},
		{"false", &mojom_types.LiteralValueBoolValue{false}},
		{"32.1", &mojom_types.LiteralValueFloatValue{float32(32.1)}},
		{"64.2", &mojom_types.LiteralValueDoubleValue{float64(64.2)}},
		{"8", &mojom_types.LiteralValueUint8Value{uint8(8)}},
		{"16", &mojom_types.LiteralValueUint16Value{uint16(16)}},
		{"32", &mojom_types.LiteralValueUint32Value{uint32(32)}},
		{"64", &mojom_types.LiteralValueUint64Value{uint64(64)}},
		{"8", &mojom_types.LiteralValueInt8Value{int8(8)}},
		{"16", &mojom_types.LiteralValueInt16Value{int16(16)}},
		{"32", &mojom_types.LiteralValueInt32Value{int32(32)}},
		{"64", &mojom_types.LiteralValueInt64Value{int64(64)}},
		{"-8", &mojom_types.LiteralValueInt8Value{int8(-8)}},
		{"-16", &mojom_types.LiteralValueInt16Value{int16(-16)}},
		{"-32", &mojom_types.LiteralValueInt32Value{int32(-32)}},
		{"-64", &mojom_types.LiteralValueInt64Value{int64(-64)}},
		{"\"hello world\"", &mojom_types.LiteralValueStringValue{"hello world"}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		value := mojom_types.ValueLiteralValue{testCase.literalValue}
		checkEq(t, testCase.expected, translator.translateValue(&value))
	}
}
