// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"sort"
	"testing"

	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

func TestTranslateMojomStruct(t *testing.T) {
	field1Name := "f_uint32"
	field1 := fidl_types.StructField{
		DeclData:   &fidl_types.DeclarationData{ShortName: &field1Name},
		Type:       &fidl_types.TypeSimpleType{Value: fidl_types.SimpleType_Uint32},
		Offset:     4,
		MinVersion: 10}

	field2Name := "f_uint16"
	field2 := fidl_types.StructField{
		DeclData:   &fidl_types.DeclarationData{ShortName: &field2Name},
		Type:       &fidl_types.TypeSimpleType{Value: fidl_types.SimpleType_Uint16},
		Offset:     5,
		MinVersion: 20}

	structName := "foo"
	s := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &structName},
		// field2 must come before field1 to test that sorting happens as expected.
		Fields: []fidl_types.StructField{field2, field1},
		VersionInfo: &[]fidl_types.StructVersion{fidl_types.StructVersion{
			VersionNumber: 10,
			NumBytes:      24,
		}},
	}

	graph := fidl_files.FidlFileGraph{}
	typeKey := "typeKey"
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		typeKey: &fidl_types.UserDefinedTypeStructType{s},
	}

	translator := NewTranslator(&graph)
	m := translator.translateMojomStruct(typeKey)

	checkEq(t, "Foo", m.Name)
	checkEq(t, uint32(16), m.CurVersionSize)
	checkEq(t, uint32(10), m.CurVersionNumber)
	checkEq(t, "FUint32", m.Fields[0].Name)
	checkEq(t, "uint32", m.Fields[0].Type)
	checkEq(t, uint32(10), m.Fields[0].MinVersion)
	checkEq(t, "FUint16", m.Fields[1].Name)
	checkEq(t, "uint16", m.Fields[1].Type)
	checkEq(t, uint32(20), m.Fields[1].MinVersion)
}

func TestFieldSerializationSorter(t *testing.T) {
	fields := []fidl_types.StructField{
		fidl_types.StructField{Offset: 3, Bit: -1},
		fidl_types.StructField{Offset: 1, Bit: 2},
		fidl_types.StructField{Offset: 2, Bit: -1},
		fidl_types.StructField{Offset: 1, Bit: 1},
		fidl_types.StructField{Offset: 1, Bit: 0},
		fidl_types.StructField{Offset: 0, Bit: -1},
	}

	sorter := structFieldSerializationSorter(fields)
	sort.Sort(sorter)

	checkEq(t, uint32(0), fields[0].Offset)
	checkEq(t, uint32(1), fields[1].Offset)
	checkEq(t, int8(0), fields[1].Bit)
	checkEq(t, uint32(1), fields[2].Offset)
	checkEq(t, int8(1), fields[2].Bit)
	checkEq(t, uint32(1), fields[3].Offset)
	checkEq(t, int8(2), fields[3].Bit)
	checkEq(t, uint32(2), fields[4].Offset)
	checkEq(t, uint32(3), fields[5].Offset)
}

func TestTranslateMojomUnion(t *testing.T) {
	field1Name := "f_uint32"
	field1 := fidl_types.UnionField{
		DeclData: &fidl_types.DeclarationData{ShortName: &field1Name},
		Type:     &fidl_types.TypeSimpleType{Value: fidl_types.SimpleType_Uint32},
		Tag:      5}

	field2Name := "f_uint16"
	field2 := fidl_types.UnionField{
		DeclData: &fidl_types.DeclarationData{ShortName: &field2Name},
		Type:     &fidl_types.TypeSimpleType{Value: fidl_types.SimpleType_Uint16},
		Tag:      6}

	unionName := "foo"
	union := fidl_types.FidlUnion{
		DeclData: &fidl_types.DeclarationData{ShortName: &unionName},
		Fields:   []fidl_types.UnionField{field1, field2},
	}

	graph := fidl_files.FidlFileGraph{}
	typeKey := "typeKey"
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		typeKey: &fidl_types.UserDefinedTypeUnionType{union},
	}

	translator := NewTranslator(&graph)

	m := translator.translateMojomUnion(typeKey)

	checkEq(t, "Foo", m.Name)
	checkEq(t, "FUint32", m.Fields[0].Name)
	checkEq(t, "uint32", m.Fields[0].Type)
	checkEq(t, uint32(5), m.Fields[0].Tag)
	checkEq(t, m, m.Fields[0].Union)
	checkEq(t, "FUint16", m.Fields[1].Name)
	checkEq(t, "uint16", m.Fields[1].Type)
	checkEq(t, uint32(6), m.Fields[1].Tag)
	checkEq(t, m, m.Fields[1].Union)
}

func TestTranslateMojomEnum(t *testing.T) {
	value1Name := "ALPHA"
	value1 := fidl_types.EnumValue{
		DeclData: &fidl_types.DeclarationData{ShortName: &value1Name},
		IntValue: int32(10)}

	value2Name := "BETA"
	value2 := fidl_types.EnumValue{
		DeclData: &fidl_types.DeclarationData{ShortName: &value2Name},
		IntValue: int32(20)}

	enumName := "SomeEnum"
	enum := fidl_types.FidlEnum{
		DeclData: &fidl_types.DeclarationData{ShortName: &enumName},
		Values:   []fidl_types.EnumValue{value1, value2}}

	graph := fidl_files.FidlFileGraph{}
	typeKey := "typeKey"
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		typeKey: &fidl_types.UserDefinedTypeEnumType{enum},
	}

	translator := NewTranslator(&graph)

	m := translator.translateMojomEnum(typeKey)

	checkEq(t, "SomeEnum", m.Name)
	checkEq(t, "SomeEnum_Alpha", m.Values[0].Name)
	checkEq(t, int32(10), m.Values[0].Value)
	checkEq(t, "SomeEnum_Beta", m.Values[1].Name)
	checkEq(t, int32(20), m.Values[1].Value)
}

func TestTranslateNestedMojomEnum(t *testing.T) {
	structName := "foo"
	structTypeKey := "structTypeKey"
	s := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &structName},
	}

	value1Name := "ALPHA"
	value1 := fidl_types.EnumValue{
		DeclData: &fidl_types.DeclarationData{ShortName: &value1Name},
		IntValue: int32(10)}

	value2Name := "BETA"
	value2 := fidl_types.EnumValue{
		DeclData: &fidl_types.DeclarationData{ShortName: &value2Name},
		IntValue: int32(20)}

	enumName := "SomeEnum"
	enumTypeKey := "enumTypeKey"
	enum := fidl_types.FidlEnum{
		DeclData: &fidl_types.DeclarationData{
			ShortName:        &enumName,
			ContainerTypeKey: &structTypeKey},
		Values: []fidl_types.EnumValue{value1, value2}}

	graph := fidl_files.FidlFileGraph{}
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		enumTypeKey:   &fidl_types.UserDefinedTypeEnumType{enum},
		structTypeKey: &fidl_types.UserDefinedTypeStructType{s},
	}

	translator := NewTranslator(&graph)

	m := translator.translateMojomEnum(enumTypeKey)

	checkEq(t, "Foo_SomeEnum", m.Name)
	checkEq(t, "Foo_SomeEnum_Alpha", m.Values[0].Name)
	checkEq(t, int32(10), m.Values[0].Value)
	checkEq(t, "Foo_SomeEnum_Beta", m.Values[1].Name)
	checkEq(t, int32(20), m.Values[1].Value)
}

func TestTranslateMojomInterface(t *testing.T) {
	interfaceTypeKey := "interfaceTypeKey"
	interfaceName := "some_interface"

	mojomInterface := fidl_types.FidlInterface{
		DeclData: &fidl_types.DeclarationData{ShortName: &interfaceName},
	}

	graph := fidl_files.FidlFileGraph{}
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		interfaceTypeKey: &fidl_types.UserDefinedTypeInterfaceType{mojomInterface},
	}

	translator := NewTranslator(&graph)

	m := translator.translateMojomInterface(interfaceTypeKey)

	checkEq(t, "SomeInterface", m.Name)
	checkEq(t, "someInterface", m.PrivateName)
}

func TestTranslateMojomMethod(t *testing.T) {
	params := fidl_types.FidlStruct{
		VersionInfo: &[]fidl_types.StructVersion{fidl_types.StructVersion{
			VersionNumber: 10,
			NumBytes:      16,
		}},
	}

	responseParams := fidl_types.FidlStruct{
		VersionInfo: &[]fidl_types.StructVersion{fidl_types.StructVersion{
			VersionNumber: 10,
			NumBytes:      16,
		}},
	}

	interfaceName := "someInterface"
	interfaceTemplate := InterfaceTemplate{PrivateName: interfaceName}
	methodName := "some_method"
	mojomMethod := fidl_types.FidlMethod{
		DeclData:       &fidl_types.DeclarationData{ShortName: &methodName},
		Parameters:     params,
		ResponseParams: &responseParams,
	}

	translator := NewTranslator(nil)

	m := translator.translateMojomMethod(mojomMethod, &interfaceTemplate)

	checkEq(t, "SomeMethod", m.MethodName)
	checkEq(t, "someInterface_SomeMethod", m.FullName)
	checkEq(t, "someInterface_SomeMethod_Params", m.Params.Name)
	checkEq(t, "someInterface_SomeMethod_ResponseParams", m.ResponseParams.Name)
}
