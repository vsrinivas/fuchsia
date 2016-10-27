// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"mojom/mojom_tool/lexer"
	"testing"
)

func checkEq(t *testing.T, expected, actual interface{}) {
	if expected != actual {
		t.Fatalf("Failed check: Expected (%v), Actual (%v)", expected, actual)
	}
}

func DummyDeclData(name string) DeclarationData {
	return DeclData(name, nil, lexer.Token{}, nil)
}

func TestMojomFileVisitor(t *testing.T) {
	descriptor := NewMojomDescriptor()
	file := newMojomFile("filename", "specifiedName", descriptor, nil, "fake source code")
	file.Attributes = NewAttributes(lexer.Token{})
	file.Attributes.List = append(file.Attributes.List, NewMojomAttribute("attr1", nil, LiteralValue{}))
	file.Attributes.List = append(file.Attributes.List, NewMojomAttribute("attr2", nil, LiteralValue{}))
	file.InitializeFileScope(NewModuleNamespace("module.ns", nil))

	file.AddImport(NewImportedFile("import1", nil))
	file.AddImport(NewImportedFile("import2", nil))
	file.AddImport(NewImportedFile("import3", nil))

	interface1 := NewMojomInterface(DummyDeclData("interface1"))
	interface1.InitAsScope(NewTestFileScope("filename"))

	method11Attrs := NewAttributes(lexer.Token{})
	method11Attrs.List = append(method11Attrs.List, NewMojomAttribute("method11_attr1", nil, LiteralValue{}))
	method11Attrs.List = append(method11Attrs.List, NewMojomAttribute("method11_attr2", nil, LiteralValue{}))
	method11 := NewMojomMethod(DeclData("method11", nil, lexer.Token{}, method11Attrs), nil, nil)
	interface1.AddMethod(method11)

	subenum11 := NewMojomEnum(DummyDeclData("subenum11"))
	interface1.AddEnum(subenum11)

	method12InParams := NewMojomStruct(DummyDeclData("method12_in_params"))
	method12InParams.InitAsScope(NewTestFileScope("filename"))
	method12InParams.AddField(NewStructField(DummyDeclData("method12_in_param1"), SimpleTypeFloat, nil))
	method12InParams.AddField(NewStructField(DummyDeclData("method12_in_param2"), SimpleTypeFloat, nil))

	method12RespParams := NewMojomStruct(DummyDeclData("method12_resp_params"))
	method12RespParams.InitAsScope(NewTestFileScope("filename"))
	method12RespParams.AddField(NewStructField(DummyDeclData("method12_resp_param1"), SimpleTypeFloat, nil))
	method12RespParams.AddField(NewStructField(DummyDeclData("method12_resp_param2"), SimpleTypeFloat, nil))
	method12 := NewMojomMethod(DummyDeclData("method12"), method12InParams, method12RespParams)
	interface1.AddMethod(method12)

	file.AddInterface(interface1)

	struct1 := NewMojomStruct(DummyDeclData("struct1"))
	struct1.InitAsScope(NewTestFileScope("filename"))

	file.AddStruct(struct1)
	structField1 := NewStructField(DummyDeclData("struct_field1"), SimpleTypeFloat, nil)
	struct1.AddField(structField1)

	union1 := NewMojomUnion(DummyDeclData("union1"))
	union1.AddField(DummyDeclData("union_field1"), SimpleTypeFloat)
	file.AddUnion(union1)

	enum1 := NewMojomEnum(DummyDeclData("enum1"))
	enum1.AddEnumValue(DummyDeclData("enum_value1"), nil)
	file.AddEnum(enum1)

	const1Attrs := NewAttributes(lexer.Token{})
	const1Attrs.List = append(const1Attrs.List, NewMojomAttribute("const1_attr1", nil, LiteralValue{}))
	const1Attrs.List = append(const1Attrs.List, NewMojomAttribute("const1_attr2", nil, LiteralValue{}))
	const1 := NewUserDefinedConstant(DeclData("const1", nil, lexer.Token{}, const1Attrs), nil, nil)
	file.AddConstant(const1)

	v := NewMojomFileVisitor(file)
	if _, ok := v.Next().(*Attributes); !ok {
		t.Fatalf("First node should have been Attributes.")
	}
	checkEq(t, "attr1", v.Next().(*MojomAttribute).Key)
	checkEq(t, "attr2", v.Next().(*MojomAttribute).Key)
	checkEq(t, "module.ns", v.Next().(*ModuleNamespace).Identifier)

	checkEq(t, "import1", v.Next().(*ImportedFile).SpecifiedName)
	checkEq(t, "import2", v.Next().(*ImportedFile).SpecifiedName)
	checkEq(t, "import3", v.Next().(*ImportedFile).SpecifiedName)

	checkEq(t, "interface1", v.Next().(*MojomInterface).DeclarationData.simpleName)

	if _, ok := v.Next().(*Attributes); !ok {
		t.Fatalf("Node before method11 should have been Attributes.")
	}
	checkEq(t, "method11_attr1", v.Next().(*MojomAttribute).Key)
	checkEq(t, "method11_attr2", v.Next().(*MojomAttribute).Key)
	checkEq(t, "method11", v.Next().(*MojomMethod).DeclarationData.simpleName)

	checkEq(t, "subenum11", v.Next().(*MojomEnum).DeclarationData.simpleName)

	checkEq(t, "method12", v.Next().(*MojomMethod).DeclarationData.simpleName)
	checkEq(t, "method12_in_param1", v.Next().(*StructField).DeclarationData.simpleName)
	checkEq(t, "method12_in_param2", v.Next().(*StructField).DeclarationData.simpleName)
	checkEq(t, "method12_resp_param1", v.Next().(*StructField).DeclarationData.simpleName)
	checkEq(t, "method12_resp_param2", v.Next().(*StructField).DeclarationData.simpleName)

	checkEq(t, "struct1", v.Next().(*MojomStruct).DeclarationData.simpleName)
	checkEq(t, "struct_field1", v.Next().(*StructField).DeclarationData.simpleName)

	checkEq(t, "union1", v.Next().(*MojomUnion).DeclarationData.simpleName)
	checkEq(t, "union_field1", v.Next().(*UnionField).DeclarationData.simpleName)

	checkEq(t, "enum1", v.Next().(*MojomEnum).DeclarationData.simpleName)
	checkEq(t, "enum_value1", v.Next().(*EnumValue).DeclarationData.simpleName)

	if _, ok := v.Next().(*Attributes); !ok {
		t.Fatalf("Node before const1 should have been Attributes.")
	}
	checkEq(t, "const1_attr1", v.Next().(*MojomAttribute).Key)
	checkEq(t, "const1_attr2", v.Next().(*MojomAttribute).Key)
	checkEq(t, "const1", v.Next().(*UserDefinedConstant).DeclarationData.simpleName)

	checkEq(t, nil, v.Next())
}

func TestEmptyMojomFile(t *testing.T) {
	descriptor := NewMojomDescriptor()
	file := newMojomFile("filename", "specifiedName", descriptor, nil, "fake source code")
	v := NewMojomFileVisitor(file)
	if el := v.Peek(); el != nil {
		t.Fatalf("An empty mojom file should have no elements. Instead got: %v", el)
	}
}
