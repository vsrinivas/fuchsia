// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

func TestSimpleTypeEncodingInfo(t *testing.T) {
	type expected struct {
		writeFunction string
		bitSize       uint32
	}
	testCases := []struct {
		simpleType fidl_types.SimpleType
		expected   expected
	}{
		{fidl_types.SimpleType_Bool, expected{"WriteBool", 1}},
		{fidl_types.SimpleType_Float, expected{"WriteFloat32", 32}},
		{fidl_types.SimpleType_Double, expected{"WriteFloat64", 64}},
		{fidl_types.SimpleType_Int8, expected{"WriteInt8", 8}},
		{fidl_types.SimpleType_Int16, expected{"WriteInt16", 16}},
		{fidl_types.SimpleType_Int32, expected{"WriteInt32", 32}},
		{fidl_types.SimpleType_Int64, expected{"WriteInt64", 64}},
		{fidl_types.SimpleType_Uint8, expected{"WriteUint8", 8}},
		{fidl_types.SimpleType_Uint16, expected{"WriteUint16", 16}},
		{fidl_types.SimpleType_Uint32, expected{"WriteUint32", 32}},
		{fidl_types.SimpleType_Uint64, expected{"WriteUint64", 64}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		actual := translator.encodingInfo(&fidl_types.TypeSimpleType{testCase.simpleType})
		checkEq(t, true, actual.IsSimple())
		checkEq(t, testCase.expected.writeFunction, actual.WriteFunction())
		checkEq(t, testCase.expected.bitSize, actual.BitSize())
	}
}

func TestStringTypeEncodingInfo(t *testing.T) {
	mojomType := &fidl_types.TypeStringType{fidl_types.StringType{Nullable: false}}
	translator := NewTranslator(nil)
	info := translator.encodingInfo(mojomType)
	checkEq(t, false, info.IsNullable())
	checkEq(t, true, info.IsSimple())
	checkEq(t, "WriteString", info.WriteFunction())
	checkEq(t, uint32(64), info.BitSize())

	mojomType.Value.Nullable = true
	info = translator.encodingInfo(mojomType)
	checkEq(t, true, info.IsNullable())
}

func TestArrayTypeEncodingInfo(t *testing.T) {
	mojomType := &fidl_types.TypeArrayType{
		fidl_types.ArrayType{
			Nullable: false,
			ElementType: &fidl_types.TypeArrayType{
				fidl_types.ArrayType{
					Nullable:    false,
					ElementType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float},
				},
			},
		}}

	translator := NewTranslator(nil)
	info := translator.encodingInfo(mojomType)

	checkEq(t, false, info.IsNullable())
	checkEq(t, true, info.IsPointer())
	checkEq(t, uint32(64), info.BitSize())
	checkEq(t, uint32(64), info.ElementEncodingInfo().BitSize())
	checkEq(t, "elem0", info.ElementEncodingInfo().Identifier())
	checkEq(t, uint32(32), info.ElementEncodingInfo().ElementEncodingInfo().BitSize())
	checkEq(t, "elem1", info.ElementEncodingInfo().ElementEncodingInfo().Identifier())
}

func TestHandleTypeEncodingInfo(t *testing.T) {
	mojomType := &fidl_types.TypeHandleType{
		fidl_types.HandleType{
			Kind:     fidl_types.HandleType_Kind_Unspecified,
			Nullable: false,
		},
	}

	translator := NewTranslator(nil)
	info := translator.encodingInfo(mojomType)

	checkEq(t, false, info.IsNullable())
	checkEq(t, true, info.IsHandle())
	checkEq(t, "ReadHandle", info.ReadFunction())

	mojomType.Value.Nullable = true

	info = translator.encodingInfo(mojomType)
	checkEq(t, true, info.IsNullable())
}

func TestMapTypeEncodingInfo(t *testing.T) {
	mojomType := &fidl_types.TypeMapType{
		fidl_types.MapType{
			Nullable:  true,
			KeyType:   &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint32},
			ValueType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Int16},
		},
	}

	translator := NewTranslator(nil)
	info := translator.encodingInfo(mojomType)
	checkEq(t, true, info.IsPointer())
	checkEq(t, true, info.IsMap())
	checkEq(t, true, info.IsNullable())
	checkEq(t, "[]uint32", info.KeyEncodingInfo().GoType())
	checkEq(t, "keys0", info.KeyEncodingInfo().Identifier())
	checkEq(t, "elem0", info.KeyEncodingInfo().ElementEncodingInfo().Identifier())
	checkEq(t, "[]int16", info.ValueEncodingInfo().GoType())
	checkEq(t, "values0", info.ValueEncodingInfo().Identifier())
	checkEq(t, "elem0", info.ValueEncodingInfo().ElementEncodingInfo().Identifier())
}

func TestStructTypeEncodingInfo(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "SomeStruct"
	typeKey := "typeKey"

	mojomStruct := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeStructType{mojomStruct}
	translator := NewTranslator(&fileGraph)

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{TypeKey: &typeKey}}

	info := translator.encodingInfo(typeRef)

	checkEq(t, true, info.IsPointer())
	checkEq(t, "SomeStruct", info.GoType())
}

func TestUnionTypeEncodingInfo(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "SomeUnion"
	typeKey := "typeKey"

	mojomUnion := fidl_types.FidlUnion{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeUnionType{mojomUnion}
	translator := NewTranslator(&fileGraph)

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{TypeKey: &typeKey}}

	info := translator.encodingInfo(typeRef)

	checkEq(t, true, info.IsUnion())
	checkEq(t, false, info.IsPointer())
	checkEq(t, uint32(128), info.BitSize())
	checkEq(t, "SomeUnion", info.GoType())

	info.(*unionTypeEncodingInfo).nestedUnion = true
	checkEq(t, true, info.IsPointer())
	checkEq(t, uint32(64), info.BitSize())
}

func TestEnumTypeEncodingInfo(t *testing.T) {
	enumName := "SomeEnum"
	enumTypeKey := "enumTypeKey"
	enum := fidl_types.FidlEnum{
		DeclData: &fidl_types.DeclarationData{ShortName: &enumName},
	}

	fileGraph := fidl_files.FidlFileGraph{}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[enumTypeKey] = &fidl_types.UserDefinedTypeEnumType{enum}

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{TypeKey: &enumTypeKey}}

	translator := NewTranslator(&fileGraph)
	info := translator.encodingInfo(typeRef)

	_ = info
}
