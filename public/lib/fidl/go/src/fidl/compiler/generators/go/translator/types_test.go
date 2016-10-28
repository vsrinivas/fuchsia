// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

func TestTranslateType(t *testing.T) {
	testCases := []struct {
		expected  string
		mojomType mojom_types.Type
	}{
		{"bool", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool}},
		{"float32", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float}},
		{"float64", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double}},
		{"int8", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8}},
		{"int16", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int16}},
		{"int32", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}},
		{"int64", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64}},
		{"uint8", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint8}},
		{"uint16", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint16}},
		{"uint32", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint32}},
		{"uint64", &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint64}},
		{"string", &mojom_types.TypeStringType{mojom_types.StringType{Nullable: false}}},
		{"*string", &mojom_types.TypeStringType{mojom_types.StringType{Nullable: true}}},
		{"system.Handle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_Unspecified, Nullable: false}}},
		{"system.MessagePipeHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_MessagePipe, Nullable: false}}},
		{"system.ConsumerHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_DataPipeConsumer, Nullable: false}}},
		{"system.ProducerHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_DataPipeProducer, Nullable: false}}},
		{"system.SharedBufferHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_SharedBuffer, Nullable: false}}},
		{"*system.Handle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_Unspecified, Nullable: true}}},
		{"*system.MessagePipeHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_MessagePipe, Nullable: true}}},
		{"*system.ConsumerHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_DataPipeConsumer, Nullable: true}}},
		{"*system.ProducerHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_DataPipeProducer, Nullable: true}}},
		{"*system.SharedBufferHandle", &mojom_types.TypeHandleType{mojom_types.HandleType{
			Kind: mojom_types.HandleType_Kind_SharedBuffer, Nullable: true}}},
		{"[]float32", &mojom_types.TypeArrayType{mojom_types.ArrayType{
			FixedLength: -1, Nullable: false,
			ElementType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float}}}},
		{"[10]float32", &mojom_types.TypeArrayType{mojom_types.ArrayType{
			FixedLength: 10, Nullable: false,
			ElementType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float}}}},
		{"*[]float32", &mojom_types.TypeArrayType{mojom_types.ArrayType{
			FixedLength: -1, Nullable: true,
			ElementType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float}}}},
		{"*[10]float32", &mojom_types.TypeArrayType{mojom_types.ArrayType{
			FixedLength: 10, Nullable: true,
			ElementType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float}}}},
		{"map[uint32]float64", &mojom_types.TypeMapType{mojom_types.MapType{
			KeyType:   &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint32},
			ValueType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double},
			Nullable:  false}}},
		{"*map[uint32]float64", &mojom_types.TypeMapType{mojom_types.MapType{
			KeyType:   &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint32},
			ValueType: &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double},
			Nullable:  true}}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		checkEq(t, testCase.expected, translator.translateType(testCase.mojomType))
	}
}

func TestTranslateReferenceType(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	enum := mojom_types.MojomEnum{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeEnumType{enum}
	translator := NewTranslator(&fileGraph)

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{TypeKey: &typeKey}}
	checkEq(t, shortName, translator.translateType(typeRef))
}

func TestTranslateNullableStructReferenceType(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	s := mojom_types.MojomStruct{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeStructType{s}
	translator := NewTranslator(&fileGraph)

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{
		TypeKey:  &typeKey,
		Nullable: true,
	}}
	checkEq(t, "*FooBar", translator.translateType(typeRef))
}

func TestTranslateNullableUnionReferenceType(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	union := mojom_types.MojomUnion{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeUnionType{union}
	translator := NewTranslator(&fileGraph)

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{
		TypeKey:  &typeKey,
		Nullable: true,
	}}
	checkEq(t, shortName, translator.translateType(typeRef))
}

func TestTranslateInterfaceType(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	i := mojom_types.MojomInterface{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName},
	}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeInterfaceType{i}

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{TypeKey: &typeKey}}

	translator := NewTranslator(&fileGraph)
	checkEq(t, "FooBar_Pointer", translator.translateType(typeRef))
}

func TestTranslateInterfaceRequestType(t *testing.T) {
	fileGraph := mojom_files.MojomFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	i := mojom_types.MojomInterface{
		DeclData: &mojom_types.DeclarationData{ShortName: &shortName},
	}
	fileGraph.ResolvedTypes = map[string]mojom_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &mojom_types.UserDefinedTypeInterfaceType{i}

	typeRef := &mojom_types.TypeTypeReference{mojom_types.TypeReference{
		TypeKey:            &typeKey,
		IsInterfaceRequest: true,
	}}

	translator := NewTranslator(&fileGraph)
	checkEq(t, "FooBar_Request", translator.translateType(typeRef))
}
