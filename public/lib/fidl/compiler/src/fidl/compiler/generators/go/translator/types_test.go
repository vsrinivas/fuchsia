// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

func TestTranslateType(t *testing.T) {
	testCases := []struct {
		expected  string
		mojomType fidl_types.Type
	}{
		{"bool", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Bool}},
		{"float32", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float}},
		{"float64", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Double}},
		{"int8", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Int8}},
		{"int16", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Int16}},
		{"int32", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Int32}},
		{"int64", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Int64}},
		{"uint8", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint8}},
		{"uint16", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint16}},
		{"uint32", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint32}},
		{"uint64", &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint64}},
		{"string", &fidl_types.TypeStringType{fidl_types.StringType{Nullable: false}}},
		{"*string", &fidl_types.TypeStringType{fidl_types.StringType{Nullable: true}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Unspecified, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Channel, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Vmo, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Process, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Thread, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Event, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Port, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Job, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Socket, Nullable: false}}},
		{"zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_EventPair, Nullable: false}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Unspecified, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Channel, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Vmo, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Process, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Thread, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Event, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Port, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Job, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_Socket, Nullable: true}}},
		{"*zx.Handle", &fidl_types.TypeHandleType{fidl_types.HandleType{
			Kind: fidl_types.HandleType_Kind_EventPair, Nullable: true}}},
		{"[]float32", &fidl_types.TypeArrayType{fidl_types.ArrayType{
			FixedLength: -1, Nullable: false,
			ElementType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float}}}},
		{"[10]float32", &fidl_types.TypeArrayType{fidl_types.ArrayType{
			FixedLength: 10, Nullable: false,
			ElementType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float}}}},
		{"*[]float32", &fidl_types.TypeArrayType{fidl_types.ArrayType{
			FixedLength: -1, Nullable: true,
			ElementType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float}}}},
		{"*[10]float32", &fidl_types.TypeArrayType{fidl_types.ArrayType{
			FixedLength: 10, Nullable: true,
			ElementType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Float}}}},
		{"map[uint32]float64", &fidl_types.TypeMapType{fidl_types.MapType{
			KeyType:   &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint32},
			ValueType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Double},
			Nullable:  false}}},
		{"*map[uint32]float64", &fidl_types.TypeMapType{fidl_types.MapType{
			KeyType:   &fidl_types.TypeSimpleType{fidl_types.SimpleType_Uint32},
			ValueType: &fidl_types.TypeSimpleType{fidl_types.SimpleType_Double},
			Nullable:  true}}},
	}

	translator := NewTranslator(nil)
	for _, testCase := range testCases {
		checkEq(t, testCase.expected, translator.translateType(testCase.mojomType))
	}
}

func TestTranslateReferenceType(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	enum := fidl_types.FidlEnum{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeEnumType{enum}
	translator := NewTranslator(&fileGraph)

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{TypeKey: &typeKey}}
	checkEq(t, shortName, translator.translateType(typeRef))
}

func TestTranslateNullableStructReferenceType(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	s := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeStructType{s}
	translator := NewTranslator(&fileGraph)

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{
		TypeKey:  &typeKey,
		Nullable: true,
	}}
	checkEq(t, "*FooBar", translator.translateType(typeRef))
}

func TestTranslateNullableUnionReferenceType(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	union := fidl_types.FidlUnion{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName}}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeUnionType{union}
	translator := NewTranslator(&fileGraph)

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{
		TypeKey:  &typeKey,
		Nullable: true,
	}}
	checkEq(t, shortName, translator.translateType(typeRef))
}

func TestTranslateInterfaceType(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	i := fidl_types.FidlInterface{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName},
	}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeInterfaceType{i}

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{TypeKey: &typeKey}}

	translator := NewTranslator(&fileGraph)
	checkEq(t, "FooBar_Pointer", translator.translateType(typeRef))
}

func TestTranslateInterfaceRequestType(t *testing.T) {
	fileGraph := fidl_files.FidlFileGraph{}
	shortName := "FooBar"
	typeKey := "typeKey"
	i := fidl_types.FidlInterface{
		DeclData: &fidl_types.DeclarationData{ShortName: &shortName},
	}
	fileGraph.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
	fileGraph.ResolvedTypes[typeKey] = &fidl_types.UserDefinedTypeInterfaceType{i}

	typeRef := &fidl_types.TypeTypeReference{fidl_types.TypeReference{
		TypeKey:            &typeKey,
		IsInterfaceRequest: true,
	}}

	translator := NewTranslator(&fileGraph)
	checkEq(t, "FooBar_Request", translator.translateType(typeRef))
}
