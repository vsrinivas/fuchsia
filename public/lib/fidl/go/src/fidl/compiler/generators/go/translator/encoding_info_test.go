// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

func TestSimpleTypeEncodingInfo(t *testing.T) {
	type expected struct {
		writeFunction string
		bitSize       uint32
	}
	testCases := []struct {
		simpleType mojom_types.SimpleType
		expected   expected
	}{
		{mojom_types.SimpleType_Bool, expected{"WriteBool", 1}},
		{mojom_types.SimpleType_Float, expected{"WriteFloat32", 32}},
		{mojom_types.SimpleType_Double, expected{"WriteFloat64", 64}},
		{mojom_types.SimpleType_Int8, expected{"WriteInt8", 8}},
		{mojom_types.SimpleType_Int16, expected{"WriteInt16", 16}},
		{mojom_types.SimpleType_Int32, expected{"WriteInt32", 32}},
		{mojom_types.SimpleType_Int64, expected{"WriteInt64", 64}},
		{mojom_types.SimpleType_Uint8, expected{"WriteUint8", 8}},
		{mojom_types.SimpleType_Uint16, expected{"WriteUint16", 16}},
		{mojom_types.SimpleType_Uint32, expected{"WriteUint32", 32}},
		{mojom_types.SimpleType_Uint64, expected{"WriteUint64", 64}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		actual := translator.encodingInfo(&mojom_types.TypeSimpleType{testCase.simpleType})
		checkEq(t, true, actual.IsSimple())
		checkEq(t, testCase.expected.writeFunction, actual.WriteFunction())
		checkEq(t, testCase.expected.bitSize, actual.BitSize())
	}
}

func TestStringTypeEncodingInfo(t *testing.T) {
	mojomType := &mojom_types.TypeStringType{mojom_types.StringType{Nullable: false}}
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
	mojomType := &mojom_types.TypeArrayType{
		mojom_types.ArrayType{
			Nullable: false,
			ElementType: &mojom_types.TypeArrayType{
				mojom_types.ArrayType{
					Nullable:    false,
					ElementType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
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
	mojomType := &mojom_types.TypeHandleType{
		mojom_types.HandleType{
			Kind:     mojom_types.HandleType_Kind_Unspecified,
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
	mojomType := &mojom_types.TypeMapType{
		mojom_types.MapType{
			Nullable:  true,
			KeyType:   &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint32},
			ValueType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int16},
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
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "SomeStruct"
	typeKey := "typeKey"

	mojomStruct := mojom_types.MojomStruct{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeStructType{mojomStruct}
	translator := NewTranslator(&fileGraph)

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{TypeKey: &typeKey}}

	info := translator.encodingInfo(typeRef)

	checkEq(t, true, info.IsPointer())
	checkEq(t, "SomeStruct", info.GoType())
}

func TestUnionTypeEncodingInfo(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "SomeUnion"
	typeKey := "typeKey"

	mojomUnion := mojom_types.MojomUnion{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeUnionType{mojomUnion}
	translator := NewTranslator(&fileGraph)

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{TypeKey: &typeKey}}

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
	enum := mojom_types.MojomEnum{
		DeclData: &mojom_types.DeclarationData{ShortName: &enumName},
	}

	fileGraph := mojom_files.MojomFileGraph{}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[enumTypeKey] = &mojom_types.UserDefinedTypeEnumType{enum}

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{TypeKey: &enumTypeKey}}

	translator := NewTranslator(&fileGraph)
	info := translator.encodingInfo(typeRef)

	_ = info
}
