// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serialization

import (
	"bytes"
	"compress/gzip"
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"mojo/public/go/bindings"
	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
	"fidl/compiler/core"
	"fidl/compiler/cmd/fidl/parser"
	myfmt "fidl/compiler/cmd/fidl/third_party/golang/src/fmt"
	"reflect"
	"testing"
)

// singleFileTestCase stores the data for one serialization test case
// in which only a single file is added to the file graph.
type singleFileTestCase struct {
	fileName             string
	mojomContents        string
	lineAndcolumnNumbers bool
	expectedFile         *mojom_files.MojomFile
	expectedGraph        *mojom_files.MojomFileGraph
}

// singleFileTest contains a series of singleFileTestCase and a current
// testCaseNum.
type singleFileTest struct {
	cases       []singleFileTestCase
	testCaseNum int
}

// expectedFile() returns the expectedFile of the current test case.
func (t *singleFileTest) expectedFile() *mojom_files.MojomFile {
	return t.cases[t.testCaseNum].expectedFile
}

// expectedGraph() returns the expectedGraph of the current test case.
func (t *singleFileTest) expectedGraph() *mojom_files.MojomFileGraph {
	return t.cases[t.testCaseNum].expectedGraph
}

// fileName() returns the fileName of the current test case
func (t *singleFileTest) fileName() string {
	return t.cases[t.testCaseNum].fileName
}

// addTestCase() should be invoked at the start of a case in
// TestSingleFileSerialization.
func (test *singleFileTest) addTestCase(moduleNameSpace, contents string) {
	fileName := fmt.Sprintf("file%d", test.testCaseNum)
	test.cases = append(test.cases, singleFileTestCase{fileName, contents, false,
		new(mojom_files.MojomFile), new(mojom_files.MojomFileGraph)})

	test.expectedFile().FileName = fileName
	test.expectedFile().SpecifiedFileName = stringPointer(fileName)
	test.expectedFile().ModuleNamespace = &moduleNameSpace

	test.expectedGraph().ResolvedTypes = make(map[string]mojom_types.UserDefinedType)
	test.expectedGraph().ResolvedConstants = make(map[string]mojom_types.DeclaredConstant)
}

// endTestCase() should be invoked at the end of a case in
// TestSingleFileSerialization.
func (test *singleFileTest) endTestCase() {
	test.expectedGraph().Files = make(map[string]mojom_files.MojomFile)
	test.expectedGraph().Files[test.fileName()] = *test.expectedFile()
	test.testCaseNum += 1
}

// newShortDeclDataO constructs a new DeclarationData with the given data.
func (test *singleFileTest) newShortDeclData(shortName string) *mojom_types.DeclarationData {
	declData := test.newContainedDeclData(shortName, "", nil)
	declData.FullIdentifier = nil
	return declData
}

// newShortDeclDataO constructs a new DeclarationData with the given data.
func (test *singleFileTest) newShortDeclDataO(declarationOrder, declaredOrdinal int32, shortName string) *mojom_types.DeclarationData {
	declData := test.newContainedDeclDataA(declarationOrder, declaredOrdinal, shortName, "", nil, nil)
	declData.FullIdentifier = nil
	return declData
}

// newDeclData constructs a new DeclarationData with the given data.
func (test *singleFileTest) newDeclData(shortName, fullIdentifier string) *mojom_types.DeclarationData {
	return test.newContainedDeclData(shortName, fullIdentifier, nil)
}

// newDeclData constructs a new DeclarationData with the given data.
func (test *singleFileTest) newDeclDataO(declarationOrder, declaredOrdinal int32, shortName, fullIdentifier string) *mojom_types.DeclarationData {
	return test.newContainedDeclDataA(declarationOrder, declaredOrdinal, shortName, fullIdentifier, nil, nil)
}

// newDeclDataA constructs a new DeclarationData with the given data, including attributes.
func (test *singleFileTest) newDeclDataA(shortName, fullIdentifier string,
	attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	return test.newContainedDeclDataA(-1, -1, shortName, fullIdentifier, nil, attributes)
}

// newShortDeclDataA constructs a new DeclarationData with the given data, including attributes.
func (test *singleFileTest) newShortDeclDataA(shortName string,
	attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	declData := test.newContainedDeclDataA(-1, -1, shortName, "", nil, attributes)
	declData.FullIdentifier = nil
	return declData
}

// newShortDeclDataA constructs a new DeclarationData with the given data, including attributes.
func (test *singleFileTest) newShortDeclDataAO(declarationOrder, declaredOrdinal int32, shortName string,
	attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	declData := test.newContainedDeclDataA(declarationOrder, declaredOrdinal, shortName, "", nil, attributes)
	declData.FullIdentifier = nil
	return declData
}

// newContainedDeclData constructs a new DeclarationData with the given data.
func (test *singleFileTest) newContainedDeclData(shortName, fullIdentifier string, containerTypeKey *string) *mojom_types.DeclarationData {
	return test.newContainedDeclDataA(-1, -1, shortName, fullIdentifier, containerTypeKey, nil)
}

// newContainedDeclDataA constructs a new DeclarationData with the given data, including attributes.
func (test *singleFileTest) newContainedDeclDataA(declarationOrder, declaredOrdinal int32, shortName, fullIdentifier string,
	containerTypeKey *string, attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	return newContainedDeclDataA(declarationOrder, declaredOrdinal, test.fileName(), shortName, fullIdentifier, containerTypeKey, attributes)
}

// newDeclData constructs a new DeclarationData with the given data.
func newDeclData(fileName, shortName, fullIdentifier string) *mojom_types.DeclarationData {
	return newContainedDeclData(fileName, shortName, fullIdentifier, nil)
}

// newDeclData constructs a new DeclarationData with the given data.
func newDeclDataO(declarationOrder, declaredOrdinal int32, fileName, shortName, fullIdentifier string) *mojom_types.DeclarationData {
	return newContainedDeclDataA(declarationOrder, declaredOrdinal, fileName, shortName, fullIdentifier, nil, nil)
}

// newDeclDataA constructs a new DeclarationData with the given data, including attributes.
func newDeclDataA(fileName, shortName, fullIdentifier string,
	attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	return newContainedDeclDataA(-1, -1, fileName, shortName, fullIdentifier, nil, attributes)
}

// newContainedDeclData constructs a new DeclarationData with the given data.
func newContainedDeclData(fileName, shortName, fullIdentifier string, containerTypeKey *string) *mojom_types.DeclarationData {
	return newContainedDeclDataA(-1, -1, fileName, shortName, fullIdentifier, containerTypeKey, nil)
}

// newContainedDeclDataA constructs a new DeclarationData with the given data, including attributes.
func newContainedDeclDataA(declarationOrder, declaredOrdinal int32, fileName, shortName, fullIdentifier string,
	containerTypeKey *string, attributes *[]mojom_types.Attribute) *mojom_types.DeclarationData {
	var fullyQualifiedName *string
	if fullIdentifier != "" {
		fullyQualifiedName = &fullIdentifier
	}
	return &mojom_types.DeclarationData{
		Attributes:       attributes,
		ShortName:        &shortName,
		FullIdentifier:   fullyQualifiedName,
		DeclaredOrdinal:  declaredOrdinal,
		DeclarationOrder: declarationOrder,
		ContainerTypeKey: containerTypeKey,
		SourceFileInfo: &mojom_types.SourceFileInfo{
			FileName: fileName,
		}}
}

// TestSingleFileSerialization uses a series of test cases in which the text of a .mojom
// file is specified and the expected MojomFileGraph is specified using Go struct literals.
func TestSingleFileSerialization(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: struct field ordinals
	////////////////////////////////////////////////////////////
	{

		contents := `
	struct Foo{
	  int32 x@2;
	  int32 y@3;
	  int32 z@0;
	  int32 w@1;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// struct Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.

				// field z
				{
					DeclData: test.newShortDeclDataO(2, 0, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(3, 1, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field x
				{
					DeclData: test.newShortDeclDataO(0, 2, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, 3, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: struct field ordinals, some implicit
	////////////////////////////////////////////////////////////
	{

		contents := `
	struct Foo{
	  int32 x@2;
	  int32 y;
	  int32 z@0;
	  int32 w;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// struct Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.

				// field z
				{
					DeclData: test.newShortDeclDataO(2, 0, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(3, -1, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field x
				{
					DeclData: test.newShortDeclDataO(0, 2, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field tags
	////////////////////////////////////////////////////////////
	{

		contents := `
	union Foo{
	  int32 x@21;
	  string y@32;
	  bool z@6;
	  float w@17;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Unions = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// union Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeUnionType{mojom_types.MojomUnion{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.UnionField{
				// The fields are in tag order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.

				// field z
				{
					DeclData: test.newShortDeclDataO(2, 6, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Tag:      6,
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(3, 17, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
					Tag:      17,
				},
				// field x
				{
					DeclData: test.newShortDeclDataO(0, 21, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Tag:      21,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, 32, "y"),
					Type:     &mojom_types.TypeStringType{mojom_types.StringType{false}},
					Tag:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field tags, some implicit
	////////////////////////////////////////////////////////////
	{

		contents := `
	union Foo{
	  int32 x@21;
	  string y;
	  bool z@6;
	  float w;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Unions = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// union Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeUnionType{mojom_types.MojomUnion{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.UnionField{
				// The fields are in tag order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.

				// field z
				{
					DeclData: test.newShortDeclDataO(2, 6, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Tag:      6,
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(3, -1, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
					Tag:      7,
				},
				// field x
				{
					DeclData: test.newShortDeclDataO(0, 21, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Tag:      21,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeStringType{mojom_types.StringType{false}},
					Tag:      22,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: array of int32
	////////////////////////////////////////////////////////////
	{

		contents := `
	struct Foo{
	  array<int32> bar1;
	  array<int32, 7> bar2;
	  array<int32>? bar3;
	  array<int32, 8>? bar4;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// struct Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// field bar1 is not nullable and not fixed length
				{
					DeclData: test.newShortDeclDataO(0, -1, "bar1"),
					Type: &mojom_types.TypeArrayType{mojom_types.ArrayType{
						false, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar2 is not nullable and fixed length of 7
				{
					DeclData: test.newShortDeclDataO(1, -1, "bar2"),
					Type: &mojom_types.TypeArrayType{mojom_types.ArrayType{
						false, 7, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar3 is nullable and not fixed length
				{
					DeclData: test.newShortDeclDataO(2, -1, "bar3"),
					Type: &mojom_types.TypeArrayType{mojom_types.ArrayType{
						true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar4 is nullable and fixed length of 8
				{
					DeclData: test.newShortDeclDataO(3, -1, "bar4"),
					Type: &mojom_types.TypeArrayType{mojom_types.ArrayType{
						true, 8, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: map string to int32
	////////////////////////////////////////////////////////////
	{

		contents := `
	struct Foo{
	  map<string,  int32>  bar1;
	  map<string?, int32>  bar2;
	  map<string,  int32>? bar3;
	  map<string?, int32>? bar4;
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// struct Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// field bar1 is non-nullable with a non-nullable key.
				{
					DeclData: test.newShortDeclDataO(0, -1, "bar1"),
					Type: &mojom_types.TypeMapType{mojom_types.MapType{
						false,
						&mojom_types.TypeStringType{mojom_types.StringType{false}},
						&mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar2 is non-nullable with a nullable key.
				{
					DeclData: test.newShortDeclDataO(1, -1, "bar2"),
					Type: &mojom_types.TypeMapType{mojom_types.MapType{
						false,
						&mojom_types.TypeStringType{mojom_types.StringType{true}},
						&mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar3 is nullable with a non-nullable key.
				{
					DeclData: test.newShortDeclDataO(2, -1, "bar3"),
					Type: &mojom_types.TypeMapType{mojom_types.MapType{
						true,
						&mojom_types.TypeStringType{mojom_types.StringType{false}},
						&mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
				// field bar4 is nullable with a nullable key.
				{
					DeclData: test.newShortDeclDataO(3, -1, "bar4"),
					Type: &mojom_types.TypeMapType{mojom_types.MapType{
						true,
						&mojom_types.TypeStringType{mojom_types.StringType{true}},
						&mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: enum value initializer
	////////////////////////////////////////////////////////////
	{

		contents := `
	enum Foo{
	  X0,
	  X1 = 42,
	  X2 = X1
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.TopLevelEnums = &[]string{"TYPE_KEY:Foo"}

		// ResolvedTypes

		// enum Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: test.newDeclData("Foo", "Foo"),
			Values: []mojom_types.EnumValue{
				// value X1
				mojom_types.EnumValue{
					DeclData: test.newDeclData("X0", "Foo.X0"),
					IntValue: 0,
				},
				// value X1
				mojom_types.EnumValue{
					DeclData:         test.newDeclData("X1", "Foo.X1"),
					InitializerValue: &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{42}},
					IntValue:         42,
				},
				// value X2
				mojom_types.EnumValue{
					DeclData: test.newDeclData("X2", "Foo.X2"),
					InitializerValue: &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
						Identifier:     "X1",
						EnumTypeKey:    "TYPE_KEY:Foo",
						EnumValueIndex: 1,
					}},
					IntValue: 42,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: enum value name is shadowed by local constant declaration.
	////////////////////////////////////////////////////////////
	{

		contents := `
	enum Color{
	  RED, BLUE
	};

	struct MyStruct {
		const Color RED = BLUE;

        Color a_color = RED; // This should resolve to the local constant RED.
	};`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.TopLevelEnums = &[]string{"TYPE_KEY:Color"}
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct"}

		// Resolved Constants

		// MyStruct.RED
		test.expectedGraph().ResolvedConstants["TYPE_KEY:MyStruct.RED"] = mojom_types.DeclaredConstant{
			DeclData: *test.newContainedDeclData("RED", "MyStruct.RED", stringPointer("TYPE_KEY:MyStruct")),
			Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
				false, false, stringPointer("Color"), stringPointer("TYPE_KEY:Color")}},
			Value: &mojom_types.ValueEnumValueReference{
				mojom_types.EnumValueReference{
					Identifier:     "BLUE",
					EnumTypeKey:    "TYPE_KEY:Color",
					EnumValueIndex: 1}},
		}

		// ResolvedTypes

		// enum Color
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Color"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: test.newDeclData("Color", "Color"),
			Values: []mojom_types.EnumValue{
				// value RED
				mojom_types.EnumValue{
					DeclData: test.newDeclData("RED", "Color.RED"),
					IntValue: 0,
				},
				// value BLUE
				mojom_types.EnumValue{
					DeclData: test.newDeclData("BLUE", "Color.BLUE"),
					IntValue: 1,
				},
			},
		}}

		// struct MyStruct
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: &mojom_types.DeclarationData{
				ShortName:        stringPointer("MyStruct"),
				FullIdentifier:   stringPointer("MyStruct"),
				DeclaredOrdinal:  -1,
				DeclarationOrder: -1,
				SourceFileInfo: &mojom_types.SourceFileInfo{
					FileName: test.fileName(),
				},
				ContainedDeclarations: &mojom_types.ContainedDeclarations{
					Constants: &[]string{"TYPE_KEY:MyStruct.RED"}},
			},
			Fields: []mojom_types.StructField{
				// field a_color
				{
					DeclData: test.newShortDeclDataO(0, -1, "a_color"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, false, stringPointer("Color"), stringPointer("TYPE_KEY:Color")}},
					DefaultValue: &mojom_types.DefaultFieldValueValue{&mojom_types.ValueConstantReference{
						mojom_types.ConstantReference{
							Identifier:  "RED",
							ConstantKey: "TYPE_KEY:MyStruct.RED"}}}, // Note this refers to MyStruct.RED and not Color.RED.
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: In-Out method parameters with the same name.
	////////////////////////////////////////////////////////////
	{

		contents := `
	module test;

	interface EchoService {
      EchoString(string? value) => (string? value);
      DelayedEchoString(string? value, int32 millis) => (string? value);
    };`

		test.addTestCase("test", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:test.EchoService"}

		// ResolvedTypes

		// interface EchoService
		test.expectedGraph().ResolvedTypes["TYPE_KEY:test.EchoService"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: test.newDeclData("EchoService", "test.EchoService"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(0, -1, "EchoString", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("EchoString-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: test.newDeclData("EchoString-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
						},
					},
				},
				1: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(1, -1, "DelayedEchoString", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("DelayedEchoString-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
							mojom_types.StructField{
								DeclData: test.newDeclDataO(1, -1, "millis", ""),
								Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: test.newDeclData("DelayedEchoString-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
						},
					},
					Ordinal: 1,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Explicit Method Ordinals. Tests the DeclarationOrder field.
	////////////////////////////////////////////////////////////
	{

		contents := `
	interface MyInterface {
      Method1@1();
      Method0@0();
    };`

		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:MyInterface"}

		// interface EchoService
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyInterface"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: test.newDeclData("MyInterface", "MyInterface"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(1, 0, "Method0", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("Method0-request", ""),
						Fields:   []mojom_types.StructField{},
					},
					Ordinal: 0,
				},
				1: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(0, 1, "Method1", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("Method1-request", ""),
						Fields:   []mojom_types.StructField{},
					},
					Ordinal: 1,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Use of the ServiceName attribute
	////////////////////////////////////////////////////////////
	{

		contents := `
       module test;

       [ServiceName = "my.test.EchoService"]
       interface EchoService {
         EchoString(string? value) => (string? value);
       };`

		test.addTestCase("test", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:test.EchoService"}

		// ResolvedTypes

		// interface EchoService
		test.expectedGraph().ResolvedTypes["TYPE_KEY:test.EchoService"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData:    test.newDeclDataA("EchoService", "test.EchoService", &[]mojom_types.Attribute{{"ServiceName", &mojom_types.LiteralValueStringValue{"my.test.EchoService"}}}),
			ServiceName: stringPointer("my.test.EchoService"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(0, -1, "EchoString", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("EchoString-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: test.newDeclData("EchoString-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: test.newDeclDataO(0, -1, "value", ""),
								Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
							},
						},
					},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Integer constants
	////////////////////////////////////////////////////////////
	{

		contents := `
	const uint8 xu8 = 255;
	const int8 x8 = -127;
	const uint16 xu16 = 0xFFFF;
	const int16 x16 = -0x7FFF;
	const uint32 xu32 = 4294967295;
	const int32 x32 = -2147483647;
	const uint64 xu64 = 0xFFFFFFFFFFFFFFFF;
	const int64 x64 = -0x7FFFFFFFFFFFFFFF;
	`
		test.addTestCase("", contents)

		// DeclaredMojomObjects

		test.expectedFile().DeclaredMojomObjects.TopLevelConstants = &[]string{
			"TYPE_KEY:xu8", "TYPE_KEY:x8", "TYPE_KEY:xu16", "TYPE_KEY:x16",
			"TYPE_KEY:xu32", "TYPE_KEY:x32", "TYPE_KEY:xu64", "TYPE_KEY:x64"}

		// Resolved Values

		// xu8
		test.expectedGraph().ResolvedConstants["TYPE_KEY:xu8"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("xu8", "xu8"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint8},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueUint8Value{255}},
		}

		// x8
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x8"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x8", "x8"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{-127}},
		}

		// xu16
		test.expectedGraph().ResolvedConstants["TYPE_KEY:xu16"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("xu16", "xu16"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint16},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueUint16Value{0xFFFF}},
		}

		// x16
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x16"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x16", "x16"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int16},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt16Value{-0x7FFF}},
		}

		// xu32
		test.expectedGraph().ResolvedConstants["TYPE_KEY:xu32"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("xu32", "xu32"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint32},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueUint32Value{4294967295}},
		}

		// x32
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x32"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x32", "x32"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt32Value{-2147483647}},
		}

		// xu64
		test.expectedGraph().ResolvedConstants["TYPE_KEY:xu64"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("xu64", "xu64"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint64},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueUint64Value{0xFFFFFFFFFFFFFFFF}},
		}

		// x64
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x64"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x64", "x64"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt64Value{-0x7FFFFFFFFFFFFFFF}},
		}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Builtin Floating-Point Constants
	////////////////////////////////////////////////////////////
	{

		contents := `
	const float f1 = float.INFINITY;
	const float f2 = float.NEGATIVE_INFINITY;
	const float f3 = float.NAN;
	const double d1 = double.INFINITY;
	const double d2 = double.NEGATIVE_INFINITY;
	const double d3 = double.NAN;
	`
		test.addTestCase("", contents)

		// DeclaredMojomObjects

		test.expectedFile().DeclaredMojomObjects.TopLevelConstants = &[]string{
			"TYPE_KEY:f1", "TYPE_KEY:f2", "TYPE_KEY:f3", "TYPE_KEY:d1", "TYPE_KEY:d2", "TYPE_KEY:d3"}

		// Resolved Values

		// f1
		test.expectedGraph().ResolvedConstants["TYPE_KEY:f1"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("f1", "f1"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_FloatInfinity},
		}

		// f2
		test.expectedGraph().ResolvedConstants["TYPE_KEY:f2"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("f2", "f2"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_FloatNegativeInfinity},
		}

		// f3
		test.expectedGraph().ResolvedConstants["TYPE_KEY:f3"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("f3", "f3"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Float},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_FloatNan},
		}

		// d1
		test.expectedGraph().ResolvedConstants["TYPE_KEY:d1"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("d1", "d1"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_DoubleInfinity},
		}

		// d2
		test.expectedGraph().ResolvedConstants["TYPE_KEY:d2"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("d2", "d2"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_DoubleNegativeInfinity},
		}

		// d3
		test.expectedGraph().ResolvedConstants["TYPE_KEY:d3"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("d3", "d3"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Double},
			Value:    &mojom_types.ValueBuiltinValue{mojom_types.BuiltinConstantValue_DoubleNan},
		}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Constants defined in terms of other constants.
	////////////////////////////////////////////////////////////
	{

		contents := `
	enum Color{
	  RED, BLUE
	};

	const int32 x1 = 42;
	const int32 x2 = x1;
	const int32 x3 = x2;

	const Color c1 = RED;
	const Color c2 = c1;
	const Color c3 = c2;
	`
		test.addTestCase("", contents)

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.TopLevelEnums = &[]string{"TYPE_KEY:Color"}
		test.expectedFile().DeclaredMojomObjects.TopLevelConstants = &[]string{"TYPE_KEY:x1", "TYPE_KEY:x2",
			"TYPE_KEY:x3", "TYPE_KEY:c1", "TYPE_KEY:c2", "TYPE_KEY:c3"}

		// Resolved Constants

		// x1
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x1"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x1", "x1"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{42}},
		}

		// x2
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x2"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x2", "x2"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
			Value: &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
				Identifier:  "x1",
				ConstantKey: "TYPE_KEY:x1"}},
			ResolvedConcreteValue: &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{42}},
		}

		// x3
		test.expectedGraph().ResolvedConstants["TYPE_KEY:x3"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("x3", "x3"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
			Value: &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
				Identifier:  "x2",
				ConstantKey: "TYPE_KEY:x2"}},
			ResolvedConcreteValue: &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{42}},
		}

		// c1
		test.expectedGraph().ResolvedConstants["TYPE_KEY:c1"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("c1", "c1"),
			Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
				false, false, stringPointer("Color"), stringPointer("TYPE_KEY:Color")}},
			Value: &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
				Identifier:     "RED",
				EnumTypeKey:    "TYPE_KEY:Color",
				EnumValueIndex: 0}},
		}

		// c2
		test.expectedGraph().ResolvedConstants["TYPE_KEY:c2"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("c2", "c2"),
			Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
				false, false, stringPointer("Color"), stringPointer("TYPE_KEY:Color")}},
			Value: &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
				Identifier:  "c1",
				ConstantKey: "TYPE_KEY:c1"}},
			ResolvedConcreteValue: &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
				EnumTypeKey:    "TYPE_KEY:Color",
				EnumValueIndex: 0}},
		}

		// c3
		test.expectedGraph().ResolvedConstants["TYPE_KEY:c3"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("c3", "c3"),
			Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
				false, false, stringPointer("Color"), stringPointer("TYPE_KEY:Color")}},
			Value: &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
				Identifier:  "c2",
				ConstantKey: "TYPE_KEY:c2"}},
			ResolvedConcreteValue: &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
				EnumTypeKey:    "TYPE_KEY:Color",
				EnumValueIndex: 0}},
		}

		// ResolvedTypes

		// enum Color
		test.expectedGraph().ResolvedTypes["TYPE_KEY:Color"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: test.newDeclData("Color", "Color"),
			Values: []mojom_types.EnumValue{
				// value RED
				mojom_types.EnumValue{
					DeclData: test.newDeclData("RED", "Color.RED"),
					IntValue: 0,
				},
				// value BLUE
				mojom_types.EnumValue{
					DeclData: test.newDeclData("BLUE", "Color.BLUE"),
					IntValue: 1,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	{

		contents := `
	[go_namespace="go.test",
	lucky=true,
	planet=EARTH]
	module core.test;

	import "another.file";
	import "and.another.file";

	const uint16 NUM_MAGI = 3;

	struct Foo{
		int32 x;
		[min_version=2]
		string y = "hello";
		string? z;

		enum Hats {
			TOP,
			COWBOY = NUM_MAGI,
			HARD,
		};
	};`

		test.addTestCase("core.test", contents)

		// Attributes
		test.expectedFile().Attributes = &[]mojom_types.Attribute{
			{"go_namespace", &mojom_types.LiteralValueStringValue{"go.test"}},
			{"lucky", &mojom_types.LiteralValueBoolValue{true}},
			{"planet", &mojom_types.LiteralValueStringValue{"EARTH"}},
		}

		// Imports
		test.expectedFile().Imports = &[]string{
			"another.file.canonical", "and.another.file.canonical",
		}

		// DeclaredMojomObjects
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:core.test.Foo"}
		test.expectedFile().DeclaredMojomObjects.TopLevelConstants = &[]string{"TYPE_KEY:core.test.NUM_MAGI"}

		// Resolved Constants

		// NUM_MAGI
		test.expectedGraph().ResolvedConstants["TYPE_KEY:core.test.NUM_MAGI"] = mojom_types.DeclaredConstant{
			DeclData: *test.newDeclData("NUM_MAGI", "core.test.NUM_MAGI"),
			Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Uint16},
			Value:    &mojom_types.ValueLiteralValue{&mojom_types.LiteralValueInt8Value{3}},
		}

		// ResolvedTypes

		// struct Foo
		test.expectedGraph().ResolvedTypes["TYPE_KEY:core.test.Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: &mojom_types.DeclarationData{
				ShortName:        stringPointer("Foo"),
				FullIdentifier:   stringPointer("core.test.Foo"),
				DeclaredOrdinal:  -1,
				DeclarationOrder: -1,
				SourceFileInfo: &mojom_types.SourceFileInfo{
					FileName: test.fileName(),
				},
				ContainedDeclarations: &mojom_types.ContainedDeclarations{
					Enums: &[]string{"TYPE_KEY:core.test.Foo.Hats"}},
			},
			Fields: []mojom_types.StructField{
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
				},
				// field y
				{
					DeclData:     test.newShortDeclDataAO(1, -1, "y", &[]mojom_types.Attribute{{"min_version", &mojom_types.LiteralValueInt8Value{2}}}),
					Type:         &mojom_types.TypeStringType{mojom_types.StringType{false}},
					DefaultValue: &mojom_types.DefaultFieldValueValue{&mojom_types.ValueLiteralValue{&mojom_types.LiteralValueStringValue{"hello"}}},
				},
				// field z
				{
					DeclData: test.newShortDeclDataO(2, -1, "z"),
					Type:     &mojom_types.TypeStringType{mojom_types.StringType{true}},
				},
			},
		}}

		// enum Hats
		test.expectedGraph().ResolvedTypes["TYPE_KEY:core.test.Foo.Hats"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: test.newContainedDeclData("Hats", "core.test.Foo.Hats", stringPointer("TYPE_KEY:core.test.Foo")),
			Values: []mojom_types.EnumValue{
				// value TOP
				mojom_types.EnumValue{
					DeclData: test.newDeclData("TOP", "core.test.Foo.Hats.TOP"),
					IntValue: 0,
				},
				// value COWBOY
				mojom_types.EnumValue{
					DeclData: test.newDeclData("COWBOY", "core.test.Foo.Hats.COWBOY"),
					IntValue: 3,
					InitializerValue: &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
						Identifier:  "NUM_MAGI",
						ConstantKey: "TYPE_KEY:core.test.NUM_MAGI",
					}},
				},
				// value HARD
				mojom_types.EnumValue{
					DeclData: test.newDeclData("HARD", "core.test.Foo.Hats.HARD"),
					IntValue: 4,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for _, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
		parser := parser.MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		if err := descriptor.Resolve(); err != nil {
			t.Errorf("Resolve error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Simulate setting the canonical file name for the imported files. In real operation
		// this step is done in parser_driver.go when each of the imported files are parsed.
		mojomFile := parser.GetMojomFile()
		if mojomFile.Imports != nil {
			for _, imp := range mojomFile.Imports {
				imp.CanonicalFileName = fmt.Sprintf("%s.canonical", imp.SpecifiedName)
			}
		}

		// Serialize
		bytes, _, err := serialize(descriptor, false, false, c.lineAndcolumnNumbers, false)
		if err != nil {
			t.Errorf("Serialization error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Serialize again and check for consistency.
		bytes2, _, err := serialize(descriptor, false, false, c.lineAndcolumnNumbers, false)
		if err != nil {
			t.Errorf("Serialization error for %s: %s", c.fileName, err.Error())
			continue
		}

		if !reflect.DeepEqual(bytes, bytes2) {
			t.Errorf("Inconsistent serialization for %s:\nbytes=%v\nbytes2=%v\n",
				c.fileName, bytes, bytes2)
			continue
		}

		// Deserialize
		decoder := bindings.NewDecoder(bytes, nil)
		fileGraph := mojom_files.MojomFileGraph{}
		fileGraph.Decode(decoder)

		// Compare
		if err := compareTwoGoObjects(c.expectedGraph, &fileGraph); err != nil {
			t.Errorf("%s:\n%s", c.fileName, err.Error())
			continue
		}
	}
}

// TestWithComputedData is similar to the previous test except that it sets
// emitComputedPackingData = true.
func TestWithComputedData(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Empty struct
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields:   []mojom_types.StructField{},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     0,
					NumBytes:      8,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test struct field min versions: 1,2
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1]
	  array<int32>? z;

      [MinVersion = 2]
	  array<int32>? w;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field z
				{
					DeclData:   test.newShortDeclDataAO(2, -1, "z", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 1,
					Offset:     8,
				},
				// field w
				{
					DeclData:   test.newShortDeclDataAO(3, -1, "w", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 2,
					Offset:     16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     2,
					NumBytes:      16,
				},
				mojom_types.StructVersion{
					VersionNumber: 1,
					NumFields:     3,
					NumBytes:      24,
				},
				mojom_types.StructVersion{
					VersionNumber: 2,
					NumFields:     4,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test struct field min versions: 1, 3
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1]
	  array<int32>? z;

      [MinVersion = 3]
	  array<int32>? w;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field z
				{
					DeclData:   test.newShortDeclDataAO(2, -1, "z", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 1,
					Offset:     8,
				},
				// field w
				{
					DeclData:   test.newShortDeclDataAO(3, -1, "w", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{3}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 3,
					Offset:     16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     2,
					NumBytes:      16,
				},
				mojom_types.StructVersion{
					VersionNumber: 1,
					NumFields:     3,
					NumBytes:      24,
				},
				mojom_types.StructVersion{
					VersionNumber: 3,
					NumFields:     4,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test struct field min versions: 1, 1
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1]
	  array<int32>? z;

      [MinVersion = 1]
	  array<int32>? w;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field z
				{
					DeclData:   test.newShortDeclDataAO(2, -1, "z", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 1,
					Offset:     8,
				},
				// field w
				{
					DeclData:   test.newShortDeclDataAO(3, -1, "w", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 1,
					Offset:     16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     2,
					NumBytes:      16,
				},
				mojom_types.StructVersion{
					VersionNumber: 1,
					NumFields:     4,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test struct field min versions: 1,2 with specified ordinals
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 y@1;

	  [MinVersion = 1]
	  array<int32>? z@2;

      [MinVersion = 2]
	  array<int32>? w@3;

	  int32 x@0;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:Foo"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:Foo"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("Foo", "Foo"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(3, 0, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(0, 1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field z
				{
					DeclData:   test.newShortDeclDataAO(1, 2, "z", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 1,
					Offset:     8,
				},
				// field w
				{
					DeclData:   test.newShortDeclDataAO(2, 3, "w", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
					Type:       &mojom_types.TypeArrayType{mojom_types.ArrayType{true, -1, &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32}}},
					MinVersion: 2,
					Offset:     16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     2,
					NumBytes:      16,
				},
				mojom_types.StructVersion{
					VersionNumber: 1,
					NumFields:     3,
					NumBytes:      24,
				},
				mojom_types.StructVersion{
					VersionNumber: 2,
					NumFields:     4,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Method  and parameter versions.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface42 {
		DoIt(int8 x0, [MinVersion=1] int64 x1, [MinVersion=2] int8 x2)
		    => (int8 y0, [MinVersion=1] int64 y1);

		[MinVersion=1]
		DoItAgain(int8 x0);

		[MinVersion=2]
		AndAgain();
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:MyInterface42"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyInterface42"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData:       test.newDeclData("MyInterface42", "MyInterface42"),
			CurrentVersion: 2,
			Methods: map[uint32]mojom_types.MojomMethod{
				// DoIt
				0: mojom_types.MojomMethod{
					DeclData: test.newDeclDataO(0, -1, "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("DoIt-request", ""),
						Fields: []mojom_types.StructField{
							// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
							// declarationOrder and declaredOrdinal.
							// x0
							mojom_types.StructField{
								DeclData:   test.newDeclDataO(0, -1, "x0", ""),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
								Offset:     0,
								MinVersion: 0,
							},
							// x1
							mojom_types.StructField{
								DeclData:   test.newShortDeclDataAO(1, -1, "x1", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64},
								Offset:     8,
								MinVersion: 1,
							},
							// x2
							mojom_types.StructField{
								DeclData:   test.newShortDeclDataAO(2, -1, "x2", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
								Offset:     1,
								MinVersion: 2,
							},
						},
						VersionInfo: &[]mojom_types.StructVersion{
							mojom_types.StructVersion{
								VersionNumber: 0,
								NumFields:     1,
								NumBytes:      16,
							},
							mojom_types.StructVersion{
								VersionNumber: 1,
								NumFields:     2,
								NumBytes:      24,
							},
							mojom_types.StructVersion{
								VersionNumber: 2,
								NumFields:     3,
								NumBytes:      24,
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: test.newDeclData("DoIt-response", ""),
						Fields: []mojom_types.StructField{
							// yo
							mojom_types.StructField{
								DeclData:   test.newDeclDataO(0, -1, "y0", ""),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
								Offset:     0,
								MinVersion: 0,
							},
							// y1
							mojom_types.StructField{
								DeclData:   test.newShortDeclDataAO(1, -1, "y1", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64},
								Offset:     8,
								MinVersion: 1,
							},
						},
						VersionInfo: &[]mojom_types.StructVersion{
							mojom_types.StructVersion{
								VersionNumber: 0,
								NumFields:     1,
								NumBytes:      16,
							},
							mojom_types.StructVersion{
								VersionNumber: 1,
								NumFields:     2,
								NumBytes:      24,
							},
						},
					},
				},

				// DoItAgain
				1: mojom_types.MojomMethod{
					DeclData: test.newShortDeclDataAO(1, -1, "DoItAgain", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("DoItAgain-request", ""),
						Fields: []mojom_types.StructField{
							// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
							// declarationOrder and declaredOrdinal.
							// x0
							mojom_types.StructField{
								DeclData:   test.newDeclDataO(0, -1, "x0", ""),
								Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
								Offset:     0,
								MinVersion: 0,
							},
						},
						VersionInfo: &[]mojom_types.StructVersion{
							mojom_types.StructVersion{
								VersionNumber: 0,
								NumFields:     1,
								NumBytes:      16,
							},
						},
					},
					Ordinal:    1,
					MinVersion: 1,
				},

				// AndAgain
				2: mojom_types.MojomMethod{
					DeclData: test.newShortDeclDataAO(2, -1, "AndAgain", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
					Parameters: mojom_types.MojomStruct{
						DeclData: test.newDeclData("AndAgain-request", ""),
						Fields:   []mojom_types.StructField{},
						VersionInfo: &[]mojom_types.StructVersion{
							mojom_types.StructVersion{
								VersionNumber: 0,
								NumFields:     0,
								NumBytes:      8,
							},
						},
					},
					Ordinal:    2,
					MinVersion: 2,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: 3 boolean fields
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct1 {
	  bool b1;
	  bool b2;
	  bool b3;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field b1
				{
					DeclData: test.newShortDeclDataO(0, -1, "b1"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      0,
				},
				// field b2
				{
					DeclData: test.newShortDeclDataO(1, -1, "b2"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      1,
				},
				// field b3
				{
					DeclData: test.newShortDeclDataO(2, -1, "b3"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      2,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     3,
					NumBytes:      16,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: int8 field + 2 boolean fields + int32field + 2 boolean fields
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct1 {
	  int8 x;

	  bool b1;
	  bool b2;

	  int32 y;

	  bool b3;
	  bool b4;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   0,
				},
				// field b1
				{
					DeclData: test.newShortDeclDataO(1, -1, "b1"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      0,
				},
				// field b2
				{
					DeclData: test.newShortDeclDataO(2, -1, "b2"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      1,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(3, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field b3
				{
					DeclData: test.newShortDeclDataO(4, -1, "b3"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      2,
				},
				// field b4
				{
					DeclData: test.newShortDeclDataO(5, -1, "b4"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      3,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     6,
					NumBytes:      16,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: int8 + int64 + bool + int32 + int8 + bool + int16
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct1 {
	  int8 x;
	  int64 y;
	  bool b1;
	  int32 z;
	  int8 w;
	  bool b2;
	  int16 r;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(1, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64},
					Offset:   8,
				},
				// field b1
				{
					DeclData: test.newShortDeclDataO(2, -1, "b1"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      0,
				},
				// field z
				{
					DeclData: test.newShortDeclDataO(3, -1, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(4, -1, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   2,
				},
				// field b2
				{
					DeclData: test.newShortDeclDataO(5, -1, "b2"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      1,
				},
				// field r
				{
					DeclData: test.newShortDeclDataO(6, -1, "r"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int16},
					Offset:   16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     7,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Specified ordinals with int8 + int64 + bool + int32 + int8 + bool + int16
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct1 {
	  int64 y@1;
	  bool b1@2;
	  int32 z@3;
	  bool b2@5;
	  int16 r@6;
	  int8 x@0;
	  int8 w@4;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(5, 0, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   0,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(0, 1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int64},
					Offset:   8,
				},
				// field b1
				{
					DeclData: test.newShortDeclDataO(1, 2, "b1"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      0,
				},
				// field z
				{
					DeclData: test.newShortDeclDataO(2, 3, "z"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   4,
				},
				// field w
				{
					DeclData: test.newShortDeclDataO(6, 4, "w"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   2,
				},
				// field b2
				{
					DeclData: test.newShortDeclDataO(3, 5, "b2"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      1,
				},
				// field r
				{
					DeclData: test.newShortDeclDataO(4, 6, "r"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int16},
					Offset:   16,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     7,
					NumBytes:      32,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Ten bools
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct1 {
	  bool b0;
	  bool b1;
	  bool b2;
	  bool b3;
	  bool b4;
	  bool b5;
	  bool b6;
	  bool b7;
	  bool b8;
	  bool b9;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				{
					DeclData: test.newShortDeclDataO(0, -1, "b0"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      0,
				},
				{
					DeclData: test.newShortDeclDataO(1, -1, "b1"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      1,
				},
				{
					DeclData: test.newShortDeclDataO(2, -1, "b2"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      2,
				},
				{
					DeclData: test.newShortDeclDataO(3, -1, "b3"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      3,
				},
				{
					DeclData: test.newShortDeclDataO(4, -1, "b4"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      4,
				},
				{
					DeclData: test.newShortDeclDataO(5, -1, "b5"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      5,
				},
				{
					DeclData: test.newShortDeclDataO(6, -1, "b6"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      6,
				},
				{
					DeclData: test.newShortDeclDataO(7, -1, "b7"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   0,
					Bit:      7,
				},
				{
					DeclData: test.newShortDeclDataO(8, -1, "b8"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      0,
				},
				{
					DeclData: test.newShortDeclDataO(9, -1, "b9"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Bool},
					Offset:   1,
					Bit:      1,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     10,
					NumBytes:      16,
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: int8 + union + struct + interface-request + int32 + interface
	//
	// x           intfcreqst  my_union---------------------------------------->  my_struct------------>  y-------->  my_interface--------->
	// 0  1  2  3  4  5  6  7  8  9  10  11  12  13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43
	//
	////////////////////////////////////////////////////////////
	{
		contents := `
	union MyUnion1{};

	interface MyInterface1{};

	struct MyStruct1 {
	  int8          x;
	  MyUnion1      my_union;
	  MyStruct1?    my_struct;
	  MyInterface1& my_interface_request;
	  int32         y;
	  MyInterface1  my_interface;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Unions = &[]string{"TYPE_KEY:MyUnion1"}
		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:MyInterface1"}
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		// MyStruct1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   0,
				},
				// field my_union
				{
					DeclData: test.newShortDeclDataO(1, -1, "my_union"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, false, stringPointer("MyUnion1"), stringPointer("TYPE_KEY:MyUnion1")}},
					Offset: 8,
				},
				// field my_struct
				{
					DeclData: test.newShortDeclDataO(2, -1, "my_struct"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						true, false, stringPointer("MyStruct1"), stringPointer("TYPE_KEY:MyStruct1")}},
					Offset: 24,
				},
				// field my_interface_request
				{
					DeclData: test.newShortDeclDataO(3, -1, "my_interface_request"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, true, stringPointer("MyInterface1"), stringPointer("TYPE_KEY:MyInterface1")}},
					Offset: 4,
				},
				// field y
				{
					DeclData: test.newShortDeclDataO(4, -1, "y"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:   32,
				},
				// field my_interface
				{
					DeclData: test.newShortDeclDataO(5, -1, "my_interface"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, false, stringPointer("MyInterface1"), stringPointer("TYPE_KEY:MyInterface1")}},
					Offset: 36,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     6,
					NumBytes:      56,
				},
			},
		}}

		// MyUnion1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyUnion1"] = &mojom_types.UserDefinedTypeUnionType{mojom_types.MojomUnion{
			DeclData: test.newDeclData("MyUnion1", "MyUnion1"),
			Fields:   []mojom_types.UnionField{},
		}}

		// MyInterface1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyInterface1"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: test.newDeclData("MyInterface1", "MyInterface1"),
			Methods:  map[uint32]mojom_types.MojomMethod{},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case: Multiple versions with int8 + union + struct + interface-request + handle + interface
	//
	// x           intfcreqst  my_union---------------------------------------->  my_struct------------>  y-------->  my_interface--------->
	// 0  1  2  3  4  5  6  7  8  9  10  11  12  13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43
	//
	////////////////////////////////////////////////////////////
	{
		contents := `
	union MyUnion1{};

	interface MyInterface1{};

	struct MyStruct1 {
	  int8          x;
	  MyUnion1      my_union;
	  MyStruct1?    my_struct;

	  [MinVersion=1]
	  MyInterface1&? my_interface_request;

	  [MinVersion=2]
	  int32       y;

	  [MinVersion=2]
	  MyInterface1?  my_interface;
	};`

		test.addTestCase("", contents)

		test.expectedFile().DeclaredMojomObjects.Unions = &[]string{"TYPE_KEY:MyUnion1"}
		test.expectedFile().DeclaredMojomObjects.Interfaces = &[]string{"TYPE_KEY:MyInterface1"}
		test.expectedFile().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:MyStruct1"}

		// MyStruct1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyStruct1"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: test.newDeclData("MyStruct1", "MyStruct1"),
			Fields: []mojom_types.StructField{
				// The fields are in ordinal order and the first two arguments to newShortDeclDataO() are
				// declarationOrder and declaredOrdinal.
				// field x
				{
					DeclData: test.newShortDeclDataO(0, -1, "x"),
					Type:     &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int8},
					Offset:   0,
				},
				// field my_union
				{
					DeclData: test.newShortDeclDataO(1, -1, "my_union"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, false, stringPointer("MyUnion1"), stringPointer("TYPE_KEY:MyUnion1")}},
					Offset: 8,
				},
				// field my_struct
				{
					DeclData: test.newShortDeclDataO(2, -1, "my_struct"),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						true, false, stringPointer("MyStruct1"), stringPointer("TYPE_KEY:MyStruct1")}},
					Offset: 24,
				},
				// field my_interface_request
				{
					DeclData: test.newShortDeclDataAO(3, -1, "my_interface_request", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{1}}}),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						true, true, stringPointer("MyInterface1"), stringPointer("TYPE_KEY:MyInterface1")}},
					Offset:     4,
					MinVersion: 1,
				},
				// field y
				{
					DeclData:   test.newShortDeclDataAO(4, -1, "y", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
					Type:       &mojom_types.TypeSimpleType{mojom_types.SimpleType_Int32},
					Offset:     32,
					MinVersion: 2,
				},
				// field my_interface
				{
					DeclData: test.newShortDeclDataAO(5, -1, "my_interface", &[]mojom_types.Attribute{{"MinVersion", &mojom_types.LiteralValueInt8Value{2}}}),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						true, false, stringPointer("MyInterface1"), stringPointer("TYPE_KEY:MyInterface1")}},
					Offset:     36,
					MinVersion: 2,
				},
			},
			VersionInfo: &[]mojom_types.StructVersion{
				mojom_types.StructVersion{
					VersionNumber: 0,
					NumFields:     3,
					NumBytes:      40,
				},
				mojom_types.StructVersion{
					VersionNumber: 1,
					NumFields:     4,
					NumBytes:      40,
				},
				mojom_types.StructVersion{
					VersionNumber: 2,
					NumFields:     6,
					NumBytes:      56,
				},
			},
		}}

		// MyUnion1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyUnion1"] = &mojom_types.UserDefinedTypeUnionType{mojom_types.MojomUnion{
			DeclData: test.newDeclData("MyUnion1", "MyUnion1"),
			Fields:   []mojom_types.UnionField{},
		}}

		// MyInterface1
		test.expectedGraph().ResolvedTypes["TYPE_KEY:MyInterface1"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: test.newDeclData("MyInterface1", "MyInterface1"),
			Methods:  map[uint32]mojom_types.MojomMethod{},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for _, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
		parser := parser.MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		if err := descriptor.Resolve(); err != nil {
			t.Errorf("Resolve error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Simulate setting the canonical file name for the imported files. In real operation
		// this step is done in parser_driver.go when each of the imported files are parsed.
		mojomFile := parser.GetMojomFile()
		if mojomFile.Imports != nil {
			for _, imp := range mojomFile.Imports {
				imp.CanonicalFileName = fmt.Sprintf("%s.canonical", imp.SpecifiedName)
			}
		}

		// Serialize. Notice that the fourth argument is |true|.
		bytes, _, err := serialize(descriptor, false, false, true, false)
		if err != nil {
			t.Errorf("Serialization error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Serialize again and check for consistency.
		bytes2, _, err := serialize(descriptor, false, false, true, false)
		if err != nil {
			t.Errorf("Serialization error for %s: %s", c.fileName, err.Error())
			continue
		}

		if !reflect.DeepEqual(bytes, bytes2) {
			t.Errorf("Inconsistent serialization for %s:\nbytes=%v\nbytes2=%v\n",
				c.fileName, bytes, bytes2)
			continue
		}

		// Deserialize
		decoder := bindings.NewDecoder(bytes, nil)
		fileGraph := mojom_files.MojomFileGraph{}
		fileGraph.Decode(decoder)

		// Compare
		if err := compareTwoGoObjects(c.expectedGraph, &fileGraph); err != nil {
			t.Errorf("%s:\n%s", c.fileName, err.Error())
			continue
		}
	}
}

// TestMetaDataOnlyMode tests parsing and serialization in meta-data-only
// mode. Only file-level attributes and the 'module' statement should be
// parsed.
func TestMetaDataOnlyMode(t *testing.T) {
	test := singleFileTest{}
	////////////////////////////////////////////////////////////
	// Test Case:
	////////////////////////////////////////////////////////////
	{
		contents := `
	[go_namespace="go.test",
	lucky=true,
	planet=EARTH]
	module core.test;

	import "another.file";
	import "and.another.file";

	const uint16 NUM_MAGI = 3;

	struct Foo{
		int32 x;
		[min_version=2]
		string y = "hello";
		string? z;

		enum Hats {
			TOP,
			COWBOY = NUM_MAGI,
			HARD,
		};
	};`

		test.addTestCase("core.test", contents)

		// Attributes
		test.expectedFile().Attributes = &[]mojom_types.Attribute{
			{"go_namespace", &mojom_types.LiteralValueStringValue{"go.test"}},
			{"lucky", &mojom_types.LiteralValueBoolValue{true}},
			{"planet", &mojom_types.LiteralValueStringValue{"EARTH"}},
		}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for _, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
		parser := parser.MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.SetMetaDataOnlyMode(true)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		if err := descriptor.Resolve(); err != nil {
			t.Errorf("Resolve error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for %s: %s", c.fileName, err.Error())
			continue
		}
		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Serialize
		bytes, _, err := serialize(descriptor, false, false, c.lineAndcolumnNumbers, false)
		if err != nil {
			t.Errorf("Serialization error for %s: %s", c.fileName, err.Error())
			continue
		}

		// Deserialize
		decoder := bindings.NewDecoder(bytes, nil)
		fileGraph := mojom_files.MojomFileGraph{}
		fileGraph.Decode(decoder)

		// Compare
		if err := compareTwoGoObjects(c.expectedGraph, &fileGraph); err != nil {
			t.Errorf("%s:\n%s", c.fileName, err.Error())
			continue
		}
	}
}

////////////////////////////////////////////
/// Two-File Tests
///////////////////////////////////////////

// twoFileTestCase stores the data for one serialization test case
// in which two files are added to the file graph.
type twoFileTestCase struct {
	// Is file B a top-level file?
	topLevel bool
	// If file A imports file B then this is the expected string that should
	// have been parsed from the import statement. Otherwise this is empty.
	importingName string
	// The contents of the two files
	mojomContentsA string
	mojomContentsB string

	lineAndcolumnNumbers         bool
	expectedFileA, expectedFileB *mojom_files.MojomFile
	expectedGraph                *mojom_files.MojomFileGraph
}

// twoFileTest contains a series of twoFileTestCases and a current
// testCaseNum.
type twoFileTest struct {
	cases       []twoFileTestCase
	testCaseNum int
}

// expectedFileA() returns the expectedFileA of the current test case.
func (t *twoFileTest) expectedFileA() *mojom_files.MojomFile {
	return t.cases[t.testCaseNum].expectedFileA
}

// expectedFileB() returns the expectedFileB of the current test case.
func (t *twoFileTest) expectedFileB() *mojom_files.MojomFile {
	return t.cases[t.testCaseNum].expectedFileB
}

// expectedGraph() returns the expectedGraph of the current test case.
func (t *twoFileTest) expectedGraph() *mojom_files.MojomFileGraph {
	return t.cases[t.testCaseNum].expectedGraph
}

func canonicalFileName(name string) string {
	return fmt.Sprintf("/my/file/system/%s", name)
}

// addTestCase() should be invoked at the start of a case in
// TestSingleFileSerialization.
// moduleNameSpace: The expected module namespace for the two parsed files.
// contentsA, contentsB: The .mojom file contents of the two files.
// topLevel: Are we simulating File B being a top-level file (as opposed to
//           one found only because it is imported from file A.)
// importingName: If non-empty, the string expected to be parsed from the
//                import statement in File A.
func (test *twoFileTest) addTestCase(moduleNameSpace, contentsA, contentsB string,
	topLevel bool, importingName string) {
	fileNameA := fmt.Sprintf("file%dA", test.testCaseNum)
	fileNameB := fmt.Sprintf("file%dB", test.testCaseNum)
	test.cases = append(test.cases, twoFileTestCase{topLevel, importingName, contentsA, contentsB, false,
		new(mojom_files.MojomFile), new(mojom_files.MojomFile), new(mojom_files.MojomFileGraph)})
	test.expectedFileA().FileName = canonicalFileName(fileNameA)
	test.expectedFileB().FileName = canonicalFileName(fileNameB)
	test.expectedFileA().SpecifiedFileName = stringPointer(fileNameA)
	specifiedNameB := ""
	if topLevel {
		// File B should have a non-empty |specified_file_name| just in case it is a top-level file.
		specifiedNameB = fileNameB
	}
	test.expectedFileB().SpecifiedFileName = stringPointer(specifiedNameB)
	test.expectedFileA().ModuleNamespace = &moduleNameSpace
	test.expectedFileB().ModuleNamespace = &moduleNameSpace

	if importingName != "" {
		// Add and import to expected File A.
		test.expectedFileA().Imports = new([]string)
		*test.expectedFileA().Imports = append(*test.expectedFileA().Imports, test.expectedFileB().FileName)
	}

	test.expectedGraph().ResolvedTypes = make(map[string]mojom_types.UserDefinedType)
	test.expectedGraph().ResolvedConstants = make(map[string]mojom_types.DeclaredConstant)
}

// endTestCase() should be invoked at the end of a case in
// TestSingleFileSerialization.
func (test *twoFileTest) endTestCase() {
	test.expectedGraph().Files = make(map[string]mojom_files.MojomFile)
	test.expectedGraph().Files[test.expectedFileA().FileName] = *test.expectedFileA()
	test.expectedGraph().Files[test.expectedFileB().FileName] = *test.expectedFileB()
	test.testCaseNum += 1
}

// TestTwoFileSerialization uses a series of test cases in which the text of two .mojom
// files is specified and the expected MojomFileGraph is specified using Go struct literals.
func TestTwoFileSerialization(t *testing.T) {
	test := twoFileTest{}

	/////////////////////////////////////////////////////////////
	// Test Case: Two top-level files with no import relationship
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};`

		contentsB := `
	module a.b.c;
	struct FooB{
	};`

		importingName := "" // File A is not importing file B.
		topLevel := true    // Simulate that File B is a top-level file.
		test.addTestCase("a.b.c", contentsA, contentsB, topLevel, importingName)

		// DeclaredMojomObjects
		test.expectedFileA().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooA"}
		test.expectedFileB().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooB"}

		// struct FooA
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileA().FileName, "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// struct FooB
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileB().FileName, "FooB", "a.b.c.FooB"),
			Fields:   []mojom_types.StructField{}}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case: Two top-level files where the first imports the second.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	import "myLittleFriend";
	struct FooA{
	};`

		contentsB := `
	module a.b.c;
	struct FooB{
	};`

		importingName := "myLittleFriend" // File A is importing File B using this name.
		topLevel := true                  // Simulate the File B is a top-level file.
		test.addTestCase("a.b.c", contentsA, contentsB, topLevel, importingName)

		// DeclaredMojomObjects
		test.expectedFileA().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooA"}
		test.expectedFileB().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooB"}

		// struct FooA
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileA().FileName, "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// struct FooB
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileB().FileName, "FooB", "a.b.c.FooB"),
			Fields:   []mojom_types.StructField{}}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case: A top-level file that imports a non-top-level file.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	import "myLittleFriend";
	struct FooA{
	};`

		contentsB := `
	module a.b.c;
	struct FooB{
	};`

		importingName := "myLittleFriend" // File A is importing File B using this name.
		topLevel := false                 // Simulate the File B is not a top-level file.
		test.addTestCase("a.b.c", contentsA, contentsB, topLevel, importingName)

		// DeclaredMojomObjects
		test.expectedFileA().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooA"}
		test.expectedFileB().DeclaredMojomObjects.Structs = &[]string{"TYPE_KEY:a.b.c.FooB"}

		// struct FooA
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileA().FileName, "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// struct FooB
		test.expectedGraph().ResolvedTypes["TYPE_KEY:a.b.c.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.expectedFileB().FileName, "FooB", "a.b.c.FooB"),
			Fields:   []mojom_types.StructField{}}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		descriptor := core.NewMojomDescriptor()

		// Parse file A.
		parserA := parser.MakeParser(c.expectedFileA.FileName, *c.expectedFileA.SpecifiedFileName, c.mojomContentsA, descriptor, nil)
		parserA.Parse()
		if !parserA.OK() {
			t.Errorf("Parsing error for %s: %s", c.expectedFileA.FileName, parserA.GetError().Error())
			continue
		}
		mojomFileA := parserA.GetMojomFile()

		// Parse file B.
		var importedFrom *core.MojomFile
		if !c.topLevel {
			// If file B is not a top-level file then when the parser for it is constructed we give it a non-nil |importedFrom|.
			importedFrom = mojomFileA
		}
		parserB := parser.MakeParser(c.expectedFileB.FileName, *c.expectedFileB.SpecifiedFileName, c.mojomContentsB, descriptor, importedFrom)
		parserB.Parse()
		if !parserB.OK() {
			t.Errorf("Parsing error for %s: %s", c.expectedFileB.FileName, parserB.GetError().Error())
			continue
		}
		mojomFileB := parserB.GetMojomFile()

		// Set the canonical file name for the imported files. In real operation
		// this step is done in parser_driver.go when each of the imported files are parsed.
		if c.importingName != "" {
			// The call to SetCanonicalImportName does a lookup in a map for a key corresponding to the
			// first argument. Thus here we are also testing that |importingName| is in fact string
			// that was parsed from the .mojom file.
			mojomFileA.SetCanonicalImportName(c.importingName, mojomFileB.CanonicalFileName)
		}

		// Resolve
		if err := descriptor.Resolve(); err != nil {
			t.Errorf("Resolve error for case %d: %s", i, err.Error())
			continue
		}
		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for case %d: %s", i, err.Error())
			continue
		}
		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for case %d: %s", i, err.Error())
			continue
		}

		// Serialize
		bytes, _, err := serialize(descriptor, false, false, c.lineAndcolumnNumbers, false)
		if err != nil {
			t.Errorf("Serialization error for case %d: %s", i, err.Error())
			continue
		}

		// Deserialize
		decoder := bindings.NewDecoder(bytes, nil)
		fileGraph := mojom_files.MojomFileGraph{}
		fileGraph.Decode(decoder)

		// Compare
		if err := compareTwoGoObjects(c.expectedGraph, &fileGraph); err != nil {
			t.Errorf("case %d:\n%s", i, err.Error())
			continue
		}
	}
}

////////////////////////////////////////////
/// Serialized Runtime Type Info Tests
///////////////////////////////////////////

type runtimeTypeInfoTestCase struct {
	// The contents of the two files
	mojomContentsA           string
	mojomContentsB           string
	expectedRuntimeTypeInfoA *mojom_types.RuntimeTypeInfo
	expectedRuntimeTypeInfoB *mojom_types.RuntimeTypeInfo
}

type runtimeTypeInfoTest struct {
	cases       []runtimeTypeInfoTestCase
	testCaseNum int
}

func (t *runtimeTypeInfoTest) expectedRuntimeTypeInfoA() *mojom_types.RuntimeTypeInfo {
	return t.cases[t.testCaseNum].expectedRuntimeTypeInfoA
}

func (t *runtimeTypeInfoTest) expectedRuntimeTypeInfoB() *mojom_types.RuntimeTypeInfo {
	return t.cases[t.testCaseNum].expectedRuntimeTypeInfoB
}

func (test *runtimeTypeInfoTest) addTestCase(contentsA, contentsB string) {
	test.cases = append(test.cases, runtimeTypeInfoTestCase{contentsA, contentsB, new(mojom_types.RuntimeTypeInfo), new(mojom_types.RuntimeTypeInfo)})
	test.expectedRuntimeTypeInfoA().Services = make(map[string]string)
	test.expectedRuntimeTypeInfoA().TypeMap = make(map[string]mojom_types.UserDefinedType)
	test.expectedRuntimeTypeInfoB().Services = make(map[string]string)
	test.expectedRuntimeTypeInfoB().TypeMap = make(map[string]mojom_types.UserDefinedType)
}

func (test *runtimeTypeInfoTest) fileNameA() string {
	return fmt.Sprintf("file%dA", test.testCaseNum)
}

func (test *runtimeTypeInfoTest) fileNameB() string {
	return fmt.Sprintf("file%dB", test.testCaseNum)
}

func (test *runtimeTypeInfoTest) endTestCase() {
	test.testCaseNum += 1
}

// TestRuntimeTypeInfo uses a series of test cases in which the text of two .mojom
// files is specified and the expected RuntimeTypeInfos are specified using Go struct literals.
func TestRuntimeTypeInfo(t *testing.T) {
	test := runtimeTypeInfoTest{}

	/////////////////////////////////////////////////////////////
	// Test Case: No nterfaces.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};`

		contentsB := `
	module b.c.d;
	struct FooB{
	};`

		test.addTestCase(contentsA, contentsB)

		// TypeMap for file A
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameA(), "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// TypeMap for file B
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameB(), "FooB", "b.c.d.FooB"),
			Fields:   []mojom_types.StructField{}}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case:  Two interfaces: Not top-level.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};

    interface InterfaceA {
    	DoIt(FooA x) => (b.c.d.FooB? y);
    };
	`

		contentsB := `
	module b.c.d;
	struct FooB{
	};

	interface InterfaceB {
    	DoIt(a.b.c.FooA x) => (FooB? y);
    };

	`
		test.addTestCase(contentsA, contentsB)

		// TypeMap for file A

		// FooA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameA(), "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.InterfaceA"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclData(test.fileNameA(), "InterfaceA", "a.b.c.InterfaceA"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameA(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("b.c.d.FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		////////////////////////////////////////////////////////////////////////

		// TypeMap for file B

		// FooA
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameB(), "FooB", "b.c.d.FooB"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceB
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.InterfaceB"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclData(test.fileNameB(), "InterfaceB", "b.c.d.InterfaceB"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameB(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("a.b.c.FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case:  Two interfaces: One of them top-level.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};

	[ServiceName = "AwesomeService"]
    interface InterfaceA {
    	DoIt(FooA x) => (b.c.d.FooB? y);
    };
	`

		contentsB := `
	module b.c.d;
	struct FooB{
	};

	interface InterfaceB {
    	DoIt(a.b.c.FooA x) => (FooB? y);
    };

	`
		test.addTestCase(contentsA, contentsB)

		// Services for file A
		test.expectedRuntimeTypeInfoA().Services["AwesomeService"] = "TYPE_KEY:a.b.c.InterfaceA"

		// TypeMap for file A

		// FooA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameA(), "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.InterfaceA"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclDataA(test.fileNameA(), "InterfaceA", "a.b.c.InterfaceA",
				&[]mojom_types.Attribute{{"ServiceName", &mojom_types.LiteralValueStringValue{"AwesomeService"}}}),
			ServiceName: stringPointer("AwesomeService"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameA(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("b.c.d.FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		////////////////////////////////////////////////////////////////////////

		// TypeMap for file B

		// FooA
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameB(), "FooB", "b.c.d.FooB"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceB
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.InterfaceB"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclData(test.fileNameB(), "InterfaceB", "b.c.d.InterfaceB"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameB(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("a.b.c.FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case:  Same as above with extra attributes.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};

	[Color="RED", ServiceName = 7, ServiceName = "AwesomeService", Height=10.1]
    interface InterfaceA {
    	DoIt(FooA x) => (b.c.d.FooB? y);
    };
	`

		contentsB := `
	module b.c.d;
	struct FooB{
	};

	[ServiceName = 42]
	interface InterfaceB {
    	DoIt(a.b.c.FooA x) => (FooB? y);
    };

	`
		test.addTestCase(contentsA, contentsB)

		// Services for file A
		test.expectedRuntimeTypeInfoA().Services["AwesomeService"] = "TYPE_KEY:a.b.c.InterfaceA"

		// FooA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameA(), "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.InterfaceA"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclDataA(test.fileNameA(), "InterfaceA", "a.b.c.InterfaceA",
				&[]mojom_types.Attribute{
					{"Color", &mojom_types.LiteralValueStringValue{"RED"}},
					{"ServiceName", &mojom_types.LiteralValueInt8Value{7}},
					{"ServiceName", &mojom_types.LiteralValueStringValue{"AwesomeService"}},
					{"Height", &mojom_types.LiteralValueDoubleValue{10.1}}}),
			ServiceName: stringPointer("AwesomeService"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameA(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("b.c.d.FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		////////////////////////////////////////////////////////////////////////

		// TypeMap for file B

		// FooA
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameB(), "FooB", "b.c.d.FooB"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceB
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.InterfaceB"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclDataA(test.fileNameB(), "InterfaceB", "b.c.d.InterfaceB",
				&[]mojom_types.Attribute{{"ServiceName", &mojom_types.LiteralValueInt8Value{42}}}),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameB(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("a.b.c.FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		test.endTestCase()
	}

	/////////////////////////////////////////////////////////////
	// Test Case:  Additional reachable types.
	/////////////////////////////////////////////////////////////
	{

		contentsA := `
	module a.b.c;
	struct FooA{
	};

	[ServiceName = "AwesomeService"]
    interface InterfaceA {
    	DoIt(FooA x) => (b.c.d.FooB? y);
    };
	`

		contentsB := `
	module b.c.d;

	enum Enum1{}; // This is in the CompleteTypeSet of InterfaceA
	enum Enum2{}; // This is not.

	struct FooB{
		Enum1 x;
	};

	interface InterfaceB {
    	DoIt(a.b.c.FooA x) => (FooB? y, Enum2 z);
    };

	`
		test.addTestCase(contentsA, contentsB)

		// Services for file A
		test.expectedRuntimeTypeInfoA().Services["AwesomeService"] = "TYPE_KEY:a.b.c.InterfaceA"

		// TypeMap for file A

		// FooA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.FooA"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameA(), "FooA", "a.b.c.FooA"),
			Fields:   []mojom_types.StructField{}}}

		// InterfaceA
		test.expectedRuntimeTypeInfoA().TypeMap["TYPE_KEY:a.b.c.InterfaceA"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclDataA(test.fileNameA(), "InterfaceA", "a.b.c.InterfaceA",
				&[]mojom_types.Attribute{{"ServiceName", &mojom_types.LiteralValueStringValue{"AwesomeService"}}}),
			ServiceName: stringPointer("AwesomeService"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameA(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameA(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameA(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("b.c.d.FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
						},
					},
				},
			},
		}}

		////////////////////////////////////////////////////////////////////////

		// TypeMap for file B

		// Enum1
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.Enum1"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: newDeclData(test.fileNameB(), "Enum1", "b.c.d.Enum1"),
			Values:   []mojom_types.EnumValue{},
		}}

		// Enum2
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.Enum2"] = &mojom_types.UserDefinedTypeEnumType{mojom_types.MojomEnum{
			DeclData: newDeclData(test.fileNameB(), "Enum2", "b.c.d.Enum2"),
			Values:   []mojom_types.EnumValue{},
		}}

		// FooA
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.FooB"] = &mojom_types.UserDefinedTypeStructType{mojom_types.MojomStruct{
			DeclData: newDeclData(test.fileNameB(), "FooB", "b.c.d.FooB"),
			Fields: []mojom_types.StructField{
				mojom_types.StructField{
					DeclData: newDeclDataO(0, -1, test.fileNameB(), "x", ""),
					Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
						false, false, stringPointer("Enum1"), stringPointer("TYPE_KEY:b.c.d.Enum1")}},
				},
			},
		}}

		// InterfaceB
		test.expectedRuntimeTypeInfoB().TypeMap["TYPE_KEY:b.c.d.InterfaceB"] = &mojom_types.UserDefinedTypeInterfaceType{mojom_types.MojomInterface{
			DeclData: newDeclData(test.fileNameB(), "InterfaceB", "b.c.d.InterfaceB"),
			Methods: map[uint32]mojom_types.MojomMethod{
				0: mojom_types.MojomMethod{
					DeclData: newDeclDataO(0, -1, test.fileNameB(), "DoIt", ""),
					Parameters: mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-request", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "x", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("a.b.c.FooA"), stringPointer("TYPE_KEY:a.b.c.FooA")}},
							},
						},
					},
					ResponseParams: &mojom_types.MojomStruct{
						DeclData: newDeclData(test.fileNameB(), "DoIt-response", ""),
						Fields: []mojom_types.StructField{
							mojom_types.StructField{
								DeclData: newDeclDataO(0, -1, test.fileNameB(), "y", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									true, false, stringPointer("FooB"), stringPointer("TYPE_KEY:b.c.d.FooB")}},
							},
							mojom_types.StructField{
								DeclData: newDeclDataO(1, -1, test.fileNameB(), "z", ""),
								Type: &mojom_types.TypeTypeReference{mojom_types.TypeReference{
									false, false, stringPointer("Enum2"), stringPointer("TYPE_KEY:b.c.d.Enum2")}},
							},
						},
					},
				},
			},
		}}

		test.endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		descriptor := core.NewMojomDescriptor()
		fileNameA := fmt.Sprintf("file%dA", i)
		fileNameB := fmt.Sprintf("file%dB", i)

		// Parse file A.
		parserA := parser.MakeParser(fileNameA, fileNameA, c.mojomContentsA, descriptor, nil)
		parserA.Parse()
		if !parserA.OK() {
			t.Errorf("Parsing error for %s: %s", fileNameA, parserA.GetError().Error())
			continue
		}

		// Parse file B.
		parserB := parser.MakeParser(fileNameB, fileNameB, c.mojomContentsB, descriptor, nil)
		parserB.Parse()
		if !parserB.OK() {
			t.Errorf("Parsing error for %s: %s", fileNameB, parserB.GetError().Error())
			continue
		}

		// Resolve
		if err := descriptor.Resolve(); err != nil {
			t.Errorf("Resolve error for case %d: %s", i, err.Error())
			continue
		}
		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for case %d: %s", i, err.Error())
			continue
		}
		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for case %d: %s", i, err.Error())
			continue
		}

		// Serialize
		bytes, _, err := serialize(descriptor, false, false, false, true)
		if err != nil {
			t.Errorf("Serialization error for case %d: %s", i, err.Error())
			continue
		}

		// Deserialize
		decoder := bindings.NewDecoder(bytes, nil)
		fileGraph := mojom_files.MojomFileGraph{}
		fileGraph.Decode(decoder)

		// Deserialize  RuntimeTypeInfo A
		runtimeTypeInfoA := deserializeRuntimeTypeInfo(*fileGraph.Files[fileNameA].SerializedRuntimeTypeInfo)

		// Deserialize  RuntimeTypeInfo B
		runtimeTypeInfoB := deserializeRuntimeTypeInfo(*fileGraph.Files[fileNameB].SerializedRuntimeTypeInfo)

		// Compare A
		if err := compareTwoGoObjects(c.expectedRuntimeTypeInfoA, &runtimeTypeInfoA); err != nil {
			t.Errorf("case %d A:\n%s", i, err.Error())
		}

		// Compare B
		if err := compareTwoGoObjects(c.expectedRuntimeTypeInfoB, &runtimeTypeInfoB); err != nil {
			t.Errorf("case %d B:\n%s", i, err.Error())
		}
	}
}

func deserializeRuntimeTypeInfo(base64String string) mojom_types.RuntimeTypeInfo {
	compressedBytes, err := base64.StdEncoding.DecodeString(base64String)
	if err != nil {
		panic(fmt.Sprintf("Error while unencoding runtimeTypeInfo: %s", err.Error()))
	}
	reader, err2 := gzip.NewReader(bytes.NewBuffer(compressedBytes))
	if err2 != nil {
		panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err.Error()))
	}
	uncompressedBytes, err3 := ioutil.ReadAll(reader)
	if err3 != nil {
		panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err2.Error()))
	}
	if err = reader.Close(); err != nil {
		panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err.Error()))
	}
	decoder := bindings.NewDecoder(uncompressedBytes, nil)
	runtimeTypeInfo := mojom_types.RuntimeTypeInfo{}
	runtimeTypeInfo.Decode(decoder)
	return runtimeTypeInfo
}

// compareTwoGoObjects compares |expected| and |actual| and returns a non-nil
// error if they are not deeply equal. The error message contains a human-readable
// string containing a deep-print of expected and actual along with the substrings
// starting from the first character where they differ.
func compareTwoGoObjects(expected interface{}, actual interface{}) error {
	if !reflect.DeepEqual(expected, actual) {
		// Note(rudominer) The myfmt package is a local modification of the fmt package
		// that does a deep printing that follows pointers for up to 50 levels.
		// Thus expectedString and actualString should contain enough information to
		// precisely capture the structure of expected and actual.
		expectedString := myfmt.Sprintf("%#v", expected)
		actualString := myfmt.Sprintf("%#v", actual)
		if expectedString != actualString {
			diffPos := -1
			for i := 0; i < len(expectedString) && i < len(actualString); i++ {
				if expectedString[i] != actualString[i] {
					diffPos = i
					break
				}
			}
			mismatchExpected := ""
			mismatchActual := ""
			if diffPos > -1 {
				mismatchExpected = expectedString[diffPos:]
				mismatchActual = actualString[diffPos:]
			}
			return fmt.Errorf("*****\nexpected=\n*****\n%q\n*****\nactual=\n*****\n%q\n*****\n"+
				"match failed at position %d: expected=\n*****\n%q\n******\nactual=\n*****\n%q\n******\n",
				expectedString, actualString, diffPos, mismatchExpected, mismatchActual)
		} else {
			return fmt.Errorf("expected != actual but the two printed equal.")
		}
	}
	return nil
}
