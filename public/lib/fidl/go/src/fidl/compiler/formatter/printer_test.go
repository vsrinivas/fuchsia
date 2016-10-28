// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package formatter

import (
	"fidl/compiler/core"
	"fidl/compiler/lexer"
	"testing"
)

func checkEq(t *testing.T, expected, actual interface{}) {
	if expected != actual {
		t.Fatalf("Failed check: Expected (%q), Actual (%q)", expected, actual)
	}
}

// Utility function to allow the printer object's initialization to get
// more complicated over time without having to modify these tests.
func getNewPrinter() *printer {
	return newPrinter()
}

func TestWriteModuleNamespace(t *testing.T) {
	m := core.NewModuleNamespace("hello.world", nil)
	p := getNewPrinter()
	p.writeModuleNamespace(m)
	checkEq(t, "module hello.world;\n", p.result())
}

func TestWriteModuleNamespaceNil(t *testing.T) {
	p := getNewPrinter()
	p.writeModuleNamespace(nil)
	checkEq(t, "", p.result())
}

func TestWriteModuleNamespaceEmpty(t *testing.T) {
	m := core.NewModuleNamespace("", nil)
	p := getNewPrinter()
	p.writeModuleNamespace(m)
	checkEq(t, "", p.result())
}

func TestWriteLiteralValue(t *testing.T) {
	testCases := []struct {
		expected string
		value    core.LiteralValue
	}{
		// TODO(azani): Make sure the escaping behavior assumed here is what the
		// parser does.
		{"\"hello \\n world\"", core.MakeStringLiteralValue("hello \n world", &lexer.Token{Text: "\"hello \\n world\""})},
		{"true", core.MakeBoolLiteralValue(true, &lexer.Token{Text: "true"})},
		{"false", core.MakeBoolLiteralValue(false, &lexer.Token{Text: "false"})},
		{"10", core.MakeInt8LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeInt16LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeInt32LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeInt64LiteralValue(10, &lexer.Token{Text: "10"})},
		{"-10", core.MakeInt64LiteralValue(-10, &lexer.Token{Text: "10"})},
		{"0x10", core.MakeInt64LiteralValue(0x10, &lexer.Token{Text: "0x10"})},
		{"10", core.MakeUint8LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeUint16LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeUint32LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10", core.MakeUint64LiteralValue(10, &lexer.Token{Text: "10"})},
		{"10.5", core.MakeDoubleLiteralValue(10.5, &lexer.Token{Text: "10.5"})},
		{"10.5", core.MakeFloatLiteralValue(10.5, &lexer.Token{Text: "10.5"})},
		{"default", core.MakeDefaultLiteral(&lexer.Token{Text: "default"})},
	}

	for _, testCase := range testCases {
		p := getNewPrinter()
		p.writeValueRef(testCase.value)
		checkEq(t, testCase.expected, p.result())
	}
}

func TestWriteAttribute(t *testing.T) {
	a := core.NewMojomAttribute("key", nil, core.MakeUint64LiteralValue(10, &lexer.Token{Text: "10"}))
	p := getNewPrinter()
	p.writeAttribute(&a)
	checkEq(t, "key=10", p.result())
}

func TestWriteAttributes(t *testing.T) {
	attrs := core.NewAttributes(lexer.Token{})
	attrs.List = append(attrs.List, core.NewMojomAttribute("key1", nil, core.MakeUint64LiteralValue(10, &lexer.Token{Text: "10"})))
	attrs.List = append(attrs.List, core.NewMojomAttribute("key2", nil, core.MakeUint64LiteralValue(20, &lexer.Token{Text: "20"})))

	p := getNewPrinter()
	p.writeAttributes(attrs)

	expected := "[key1=10,\n key2=20]\n"
	checkEq(t, expected, p.result())
}

func TestWriteAttributesSingleLine(t *testing.T) {
	attrs := core.NewAttributes(lexer.Token{})
	attrs.List = append(attrs.List, core.NewMojomAttribute("key1", nil, core.MakeUint64LiteralValue(10, &lexer.Token{Text: "10"})))
	attrs.List = append(attrs.List, core.NewMojomAttribute("key2", nil, core.MakeUint64LiteralValue(20, &lexer.Token{Text: "20"})))

	p := getNewPrinter()
	p.writeAttributesSingleLine(attrs)

	expected := "[key1=10, key2=20] "
	checkEq(t, expected, p.result())
}

func TestWriteImportedFiles(t *testing.T) {
	imports := []*core.ImportedFile{
		core.NewImportedFile("foo2.mojom", nil),
		core.NewImportedFile("foo1.mojom", nil),
	}

	p := getNewPrinter()
	p.writeImportedFiles(imports)

	expected := "import \"foo1.mojom\";\nimport \"foo2.mojom\";\n"
	checkEq(t, expected, p.result())
}

func TestWriteImportedFilesBlocks(t *testing.T) {
	imports := []*core.ImportedFile{
		core.NewImportedFile("foo4.mojom", nil),
		core.NewImportedFile("foo3.mojom", nil),
		core.NewImportedFile("foo2.mojom", nil),
		core.NewImportedFile("foo1.mojom", nil),
	}
	c := imports[2].NewAttachedComments()
	c.Above = append(c.Above, lexer.Token{Kind: lexer.EmptyLine})

	p := getNewPrinter()
	p.writeImportedFiles(imports)

	expected := `import "foo3.mojom";
import "foo4.mojom";

import "foo1.mojom";
import "foo2.mojom";
`
	checkEq(t, expected, p.result())
}

func TestWriteSingleLineComment(t *testing.T) {
	commentText := "// Hello world."
	token := lexer.Token{Kind: lexer.SingleLineComment, Text: commentText}
	p := getNewPrinter()
	p.writeSingleLineComment(token)
	checkEq(t, commentText, p.result())
}

func TestWriteSingleLineCommentAddSpace(t *testing.T) {
	commentText := "//Hello world."
	token := lexer.Token{Kind: lexer.SingleLineComment, Text: commentText}
	p := getNewPrinter()
	p.writeSingleLineComment(token)
	checkEq(t, "// Hello world.", p.result())
}

func TestWriteMultilineComments(t *testing.T) {
	commentText := `/*
 * Some comment.
 * More comments.
 * Even more comments.

 * And after a space.
 */`
	token := lexer.Token{Kind: lexer.MultiLineComment, Text: commentText}
	p := getNewPrinter()
	p.writeMultiLineComment(token)
	checkEq(t, commentText, p.result())
}

func TestWriteMultilineCommentsWrapping(t *testing.T) {
	commentText := `/*
 * Some comment more words and even more words and even more words and more words more words.
 * More comments.
 * Even more comments.

 * And after a space.
 */`
	expected := `/*
 * Some comment more words and even more words and even more words and more
 * words more words.
 * More comments.
 * Even more comments.

 * And after a space.
 */`
	token := lexer.Token{Kind: lexer.MultiLineComment, Text: commentText}
	p := getNewPrinter()
	p.writeMultiLineComment(token)
	checkEq(t, expected, p.result())
}

func cppComment(line int, c string) (t lexer.Token) {
	t.Kind = lexer.SingleLineComment
	t.Text = "// " + c
	t.LineNo = line
	return
}

func cComment(line int, c string) (t lexer.Token) {
	t.Kind = lexer.MultiLineComment
	t.Text = "/* " + c + " */"
	t.LineNo = line
	return
}

func TestWriteComments(t *testing.T) {
	comments := []lexer.Token{
		cppComment(0, "block 1 line 1"),
		cppComment(1, "block 1 line 2"),
		cppComment(2, "block 1 line 3"),

		lexer.Token{Kind: lexer.EmptyLine},

		cComment(10, "block 2 line 1\nblock 2 line 2\nblock 2 line 3"),

		lexer.Token{Kind: lexer.EmptyLine},

		cppComment(20, "block 3 line 1"),
		cppComment(21, "block 3 line 2"),
		cppComment(22, "block 3 line 3"),
	}
	p := getNewPrinter()
	p.writeCommentBlocks(comments, true)

	expected := `// block 1 line 1
// block 1 line 2
// block 1 line 3

/* block 2 line 1
   block 2 line 2
   block 2 line 3 */

// block 3 line 1
// block 3 line 2
// block 3 line 3
`

	checkEq(t, expected, p.result())
}

func TestWriteTypeRefBuiltIns(t *testing.T) {
	testCases := []string{
		"string",
		"handle",
		"handle<message_pipe>",
		"handle<data_pipe_consumer>",
		"handle<data_pipe_producer>",
		"handle<shared_buffer>",
		"string?",
		"handle?",
		"handle<message_pipe>?",
		"handle<data_pipe_consumer>?",
		"handle<data_pipe_producer>?",
		"handle<shared_buffer>?",
		"bool",
		"int8",
		"int16",
		"int32",
		"int64",
		"uint8",
		"uint16",
		"uint32",
		"uint64",
	}

	for _, testCase := range testCases {
		p := getNewPrinter()
		p.writeTypeRef(core.BuiltInType(testCase))
		checkEq(t, testCase, p.result())
	}
}

func TestWriteTypeRefCompositesAndUserDefined(t *testing.T) {
	testCases := []struct {
		typeRef  core.TypeRef
		expected string
	}{
		{core.NewArrayTypeRef(core.BuiltInType("int8"), -1, false), "array<int8>"},
		{core.NewArrayTypeRef(core.BuiltInType("int8"), 10, false), "array<int8, 10>"},
		{core.NewArrayTypeRef(core.BuiltInType("int8"), -1, true), "array<int8>?"},
		{core.NewArrayTypeRef(core.BuiltInType("int8"), 10, true), "array<int8, 10>?"},
		{core.NewMapTypeRef(core.BuiltInType("int8"), core.BuiltInType("int16"), false),
			"map<int8, int16>"},
		{core.NewMapTypeRef(core.BuiltInType("int8"), core.BuiltInType("int16"), true),
			"map<int8, int16>?"},
		{core.NewUserTypeRef("Foo", false, false, nil, lexer.Token{}), "Foo"},
		{core.NewUserTypeRef("Foo", true, false, nil, lexer.Token{}), "Foo?"},
		{core.NewUserTypeRef("Foo", false, true, nil, lexer.Token{}), "Foo&"},
		{core.NewUserTypeRef("Foo", true, true, nil, lexer.Token{}), "Foo&?"},
		{core.NewArrayTypeRef(core.NewUserTypeRef("Foo", false, false, nil, lexer.Token{}), -1, false), "array<Foo>"},
		{core.NewMapTypeRef(
			core.NewUserTypeRef("Foo", false, false, nil, lexer.Token{}),
			core.NewUserTypeRef("Bar", false, false, nil, lexer.Token{}), false),
			"map<Foo, Bar>"},
	}
	for _, testCase := range testCases {
		p := getNewPrinter()
		p.writeTypeRef(testCase.typeRef)
		checkEq(t, testCase.expected, p.result())
	}
}

func TestWriteUserValueRef(t *testing.T) {
	userValueRef := core.NewUserValueRef(core.AssigneeSpec{}, "identifier", nil, lexer.Token{})
	p := getNewPrinter()
	p.writeValueRef(userValueRef)
	checkEq(t, "identifier", p.result())

}

func TestWriteStructField(t *testing.T) {
	structField := core.NewStructField(
		core.DeclDataWithOrdinal("", nil, lexer.Token{Text: "field1"}, nil, 5),
		core.BuiltInType("int8"),
		core.MakeInt8LiteralValue(10, &lexer.Token{Text: "10"}))

	p := getNewPrinter()
	p.writeDeclaredObject(structField)
	checkEq(t, "int8 field1@5 = 10;", p.result())
}

func TestWriteMojomStruct(t *testing.T) {
	declData := core.DeclData("", nil, lexer.Token{Text: "FooStruct"}, nil)
	mojomStruct := core.NewMojomStruct(declData)
	mojomStruct.InitAsScope(core.NewTestFileScope("test.scope"))

	mojomStruct.AddField(core.NewStructField(
		core.DeclData("field1", nil, lexer.Token{Text: "field1"}, nil),
		core.BuiltInType("int8"),
		core.MakeInt8LiteralValue(10, &lexer.Token{Text: "10"})))

	mojomStruct.AddField(core.NewStructField(
		core.DeclData("field2", nil, lexer.Token{Text: "field2"}, nil),
		core.BuiltInType("string"),
		nil))

	expected := `struct FooStruct {
  int8 field1 = 10;
  string field2;
};`

	p := getNewPrinter()
	p.writeDeclaredObject(mojomStruct)
	checkEq(t, expected, p.result())
}

func TestWriteMojomStructEmpty(t *testing.T) {
	declData := core.DeclData("", nil, lexer.Token{Text: "FooStruct"}, nil)
	mojomStruct := core.NewMojomStruct(declData)

	expected := "struct FooStruct {};"

	p := getNewPrinter()
	p.writeDeclaredObject(mojomStruct)
	checkEq(t, expected, p.result())
}

func TestWriteUnionField(t *testing.T) {
	unionField := &core.UnionField{FieldType: core.BuiltInType("int8")}
	unionField.DeclarationData = core.DeclDataWithOrdinal("field1", nil, lexer.Token{Text: "field1"}, nil, 5)
	p := getNewPrinter()
	p.writeUnionField(unionField)
	checkEq(t, "int8 field1@5;", p.result())
}

func TestWriteMojomUnion(t *testing.T) {
	declData := core.DeclData("", nil, lexer.Token{Text: "FooUnion"}, nil)
	mojomUnion := core.NewMojomUnion(declData)

	mojomUnion.AddField(
		core.DeclDataWithOrdinal("field1", nil, lexer.Token{Text: "field1"}, nil, 5),
		core.BuiltInType("int8"))

	mojomUnion.AddField(
		core.DeclData("field2", nil, lexer.Token{Text: "field2"}, nil),
		core.BuiltInType("string"))

	expected := `union FooUnion {
  int8 field1@5;
  string field2;
};`
	p := getNewPrinter()
	p.writeDeclaredObject(mojomUnion)
	checkEq(t, expected, p.result())
}

func TestWriteMojomEnum(t *testing.T) {
	declData := core.DeclData("", nil, lexer.Token{Text: "FooEnum"}, nil)
	mojomEnum := core.NewMojomEnum(declData)

	mojomEnum.AddEnumValue(
		core.DeclData("VAL1", nil, lexer.Token{Text: "VAL1"}, nil),
		nil)

	mojomEnum.AddEnumValue(
		core.DeclData("VAL2", nil, lexer.Token{Text: "VAL2"}, nil),
		core.MakeInt32LiteralValue(10, &lexer.Token{Text: "10"}))

	expected := `enum FooEnum {
  VAL1,
  VAL2 = 10,
};`

	p := getNewPrinter()
	p.writeDeclaredObject(mojomEnum)
	checkEq(t, expected, p.result())
}

func TestWriteUserDefinedConstant(t *testing.T) {
	declData := core.DeclData("", nil, lexer.Token{Text: "const_foo"}, nil)
	constant := core.NewUserDefinedConstant(
		declData,
		core.BuiltInType("int8"),
		core.MakeInt8LiteralValue(10, &lexer.Token{Text: "10"}))

	p := getNewPrinter()
	p.writeDeclaredObject(constant)
	checkEq(t, "const int8 const_foo = 10;", p.result())
}

func TestWriteMojomMethod(t *testing.T) {
	params := core.NewMojomStruct(core.DeclTestData("dummy"))
	params.InitAsScope(core.NewTestFileScope("test.scope"))
	params.AddField(core.NewStructField(core.DeclTestData("param1"), core.SimpleTypeInt8, nil))
	params.AddField(core.NewStructField(core.DeclTestData("param2"), core.SimpleTypeInt16, nil))
	responseParams := core.NewMojomStruct(core.DeclTestData("dummy"))
	responseParams.InitAsScope(core.NewTestFileScope("test.scope"))
	responseParams.AddField(core.NewStructField(core.DeclTestData("rparam1"), core.SimpleTypeInt32, nil))
	responseParams.AddField(core.NewStructField(core.DeclTestData("rparam2"), core.SimpleTypeInt64, nil))

	mojomMethod := core.NewMojomMethod(core.DeclTestData("method_foo"), params, responseParams)

	p := getNewPrinter()
	p.writeDeclaredObject(mojomMethod)
	checkEq(t, "method_foo(int8 param1, int16 param2) => (int32 rparam1, int64 rparam2);", p.result())
}
