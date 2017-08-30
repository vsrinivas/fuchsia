// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"

	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

func TestFormatName(t *testing.T) {
	testCases := []struct {
		expected string
		name     string
	}{
		{"Hello", "hello"},
		{"Hello", "Hello"},
		{"HelloWorld", "HelloWorld"},
		{"HelloWorld", "hello_world"},
		{"HttpWorld", "HTTP_World"},
		{"NetAddressIPv4", "NetAddressIPv4"},
		{"Ipv4", "IPV4"},
	}

	for _, testCase := range testCases {
		checkEq(t, testCase.expected, formatName(testCase.name))
	}
}

func TestPrivateName(t *testing.T) {
	testCases := []struct {
		expected string
		public   string
	}{
		{"hello", "hello"},
		{"hello", "Hello"},
	}

	for _, testCase := range testCases {
		checkEq(t, testCase.expected, privateName(testCase.public))
	}
}

func TestGoTypeName(t *testing.T) {
	structName := "foo"
	s := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &structName},
	}

	graph := fidl_files.FidlFileGraph{}
	typeKey := "typeKey"
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		typeKey: &fidl_types.UserDefinedTypeStructType{s},
	}

	translator := NewTranslator(&graph)

	checkEq(t, "Foo", translator.goTypeName(typeKey))
}

func TestGoTypeNameNestedEnum(t *testing.T) {
	structName := "foo"
	structTypeKey := "structTypeKey"
	s := fidl_types.FidlStruct{
		DeclData: &fidl_types.DeclarationData{ShortName: &structName},
	}

	enumName := "some_enum"
	enumTypeKey := "enumTypeKey"
	e := fidl_types.FidlEnum{
		DeclData: &fidl_types.DeclarationData{
			ShortName:        &enumName,
			ContainerTypeKey: &structTypeKey,
		},
	}

	graph := fidl_files.FidlFileGraph{}
	graph.ResolvedTypes = map[string]fidl_types.UserDefinedType{
		structTypeKey: &fidl_types.UserDefinedTypeStructType{s},
		enumTypeKey:   &fidl_types.UserDefinedTypeEnumType{e},
	}

	translator := NewTranslator(&graph)

	checkEq(t, "Foo_SomeEnum", translator.goTypeName(enumTypeKey))
}

func TestFileNameToPackageName(t *testing.T) {
	fileName := "a/b/c/blah.mojom"
	expected := "blah"
	checkEq(t, expected, fileNameToPackageName(fileName))
}
