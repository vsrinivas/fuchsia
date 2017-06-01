// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	//"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

func TestTranslateLiteralValue(t *testing.T) {
	testCases := []struct {
		expected     string
		literalValue fidl_types.LiteralValue
	}{
		{"true", &fidl_types.LiteralValueBoolValue{true}},
		{"false", &fidl_types.LiteralValueBoolValue{false}},
		{"32.1", &fidl_types.LiteralValueFloatValue{float32(32.1)}},
		{"64.2", &fidl_types.LiteralValueDoubleValue{float64(64.2)}},
		{"8", &fidl_types.LiteralValueUint8Value{uint8(8)}},
		{"16", &fidl_types.LiteralValueUint16Value{uint16(16)}},
		{"32", &fidl_types.LiteralValueUint32Value{uint32(32)}},
		{"64", &fidl_types.LiteralValueUint64Value{uint64(64)}},
		{"8", &fidl_types.LiteralValueInt8Value{int8(8)}},
		{"16", &fidl_types.LiteralValueInt16Value{int16(16)}},
		{"32", &fidl_types.LiteralValueInt32Value{int32(32)}},
		{"64", &fidl_types.LiteralValueInt64Value{int64(64)}},
		{"-8", &fidl_types.LiteralValueInt8Value{int8(-8)}},
		{"-16", &fidl_types.LiteralValueInt16Value{int16(-16)}},
		{"-32", &fidl_types.LiteralValueInt32Value{int32(-32)}},
		{"-64", &fidl_types.LiteralValueInt64Value{int64(-64)}},
		{"\"hello world\"", &fidl_types.LiteralValueStringValue{"hello world"}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		value := fidl_types.ValueLiteralValue{testCase.literalValue}
		checkEq(t, testCase.expected, translator.translateValue(&value))
	}
}
