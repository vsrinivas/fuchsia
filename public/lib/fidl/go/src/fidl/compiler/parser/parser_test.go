// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fidl/compiler/core"
	"fidl/compiler/lexer"
	"fmt"
	"strings"
	"testing"
)

// TestSuccessfulParsing contains a series of test cases in which we
// run the parser on a valid mojom input string and compare the resulting
// MojomFile to an expected one.
func TestSuccessfulParsing(t *testing.T) {
	type testCase struct {
		fileName      string
		mojomContents string
		expectedFile  *core.MojomFile
	}
	cases := make([]testCase, 0)
	testCaseNum := 0
	var expectedFile *core.MojomFile

	startTestCase := func(moduleNameSpace string) {
		descriptor := core.NewMojomDescriptor()
		fileName := fmt.Sprintf("file%d", testCaseNum)
		expectedFile = descriptor.AddMojomFile(fileName, fileName, nil, "")
		expectedFile.InitializeFileScope(core.NewModuleNamespace(moduleNameSpace, nil))
		cases = append(cases, testCase{fileName, "", expectedFile})
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	// Note(rudominer) The structure of this method is designed to allow
	// test cases to be rearranged and new test cases to be inserted at
	// arbitrary locations. Do not hard-code anything that refers to the
	// position of a test case in the list.

	////////////////////////////////////////////////////////////
	// Test Case (empty file)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = ""
	endTestCase()
	////////////////////////////////////////////////////////////
	// Test Case (module statement only)
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `module core.test;`
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (module statement with attributes)
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `[cool=true]module core.test;`
	expectedFile.Attributes = core.NewAttributes(lexer.Token{})
	expectedFile.Attributes.List = append(expectedFile.Attributes.List,
		core.NewMojomAttribute("cool", nil, core.MakeBoolLiteralValue(true, nil)))
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (import statements only)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `import "a.file";`
	expectedFile.AddImport(core.NewImportedFile("a.file", nil))
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (module and import statements only)
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	module core.test;

	import "a.file";`
	{
		expectedFile.AddImport(core.NewImportedFile("a.file", nil))
		endTestCase()
	}

	////////////////////////////////////////////////////////////
	// Test Case (module with attributes and import statements only)
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	[cool=true]
	module core.test;

	import "a.file";`
	{
		expectedFile.Attributes = core.NewAttributes(lexer.Token{})
		expectedFile.Attributes.List = append(expectedFile.Attributes.List,
			core.NewMojomAttribute("cool", nil, core.MakeBoolLiteralValue(true, nil)))
		expectedFile.AddImport(core.NewImportedFile("a.file", nil))
		endTestCase()
	}
	////////////////////////////////////////////////////////////
	// Test Case (one empty sruct)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `struct Foo{};`
	expectedFile.AddStruct(core.NewMojomStruct(core.DeclTestData("Foo")))
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Integer constants)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const uint8 xu8 = 255;
	const int8 x8 = -127;
	const uint16 xu16 = 0xFFFF;
	const int16 x16 = -0x7FFF;
	const uint32 xu32 = 4294967295;
	const int32 x32 = -2147483647;
	const uint64 xu64 = 0xFFFFFFFFFFFFFFFF;
	const int64 x64 = -0x7FFFFFFFFFFFFFFF;
	const uint64 manyNines = 9999999999999999999;
	`
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("xu8"),
		core.SimpleTypeUInt8, core.MakeUint8LiteralValue(0xFF, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("x8"),
		core.SimpleTypeInt8, core.MakeInt8LiteralValue(-0x7F, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("xu16"),
		core.SimpleTypeUInt16, core.MakeUint16LiteralValue(65535, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("x16"),
		core.SimpleTypeInt16, core.MakeInt16LiteralValue(-32767, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("xu32"),
		core.SimpleTypeUInt32, core.MakeUint32LiteralValue(0xFFFFFFFF, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("x32"),
		core.SimpleTypeInt32, core.MakeInt32LiteralValue(-0x7FFFFFFF, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("xu64"),
		core.SimpleTypeUInt64, core.MakeUint64LiteralValue(18446744073709551615, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("x64"),
		core.SimpleTypeInt64, core.MakeInt64LiteralValue(-9223372036854775807, nil)))
	expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("manyNines"),
		core.SimpleTypeUInt64, core.MakeUint64LiteralValue(9999999999999999999, nil)))
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (float and double constants)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const float x = 123.456E7;
	const float y = 123456789.123456789;
	const float z = -0.01;
	const double w = -0.01;
	const double r = 3.14159E40;
	`
	{
		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("x"),
			core.SimpleTypeFloat, core.MakeDoubleLiteralValue(1234560000, nil)))
		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("y"),
			core.SimpleTypeFloat, core.MakeDoubleLiteralValue(123456789.123456789, nil)))
		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("z"),
			core.SimpleTypeFloat, core.MakeDoubleLiteralValue(-0.01, nil)))
		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("w"),
			core.SimpleTypeDouble, core.MakeDoubleLiteralValue(-0.01, nil)))
		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("r"),
			core.SimpleTypeDouble, core.MakeDoubleLiteralValue(3.14159e+40, nil)))
	}
	endTestCase()
	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	module core.test;

	struct Foo{
		int32 x;
	};`
	{
		structFoo := core.NewMojomStruct(core.DeclTestData("Foo"))
		structFoo.InitAsScope(core.NewTestFileScope("test.scope"))
		structFoo.AddField(core.NewStructField(core.DeclTestData("x"), core.SimpleTypeInt32, nil))
		expectedFile.AddStruct(structFoo)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	module core.test;

	import "another.file";
	import "and.another.file";

	struct Foo{
		[happy=true] int32 x@0;
	};`
	{
		expectedFile.AddImport(core.NewImportedFile("another.file", nil))
		expectedFile.AddImport(core.NewImportedFile("and.another.file", nil))

		structFoo := core.NewMojomStruct(core.DeclTestData("Foo"))
		structFoo.InitAsScope(core.NewTestFileScope("test.scope"))
		attributes := core.NewAttributes(lexer.Token{})
		attributes.List = append(attributes.List, core.NewMojomAttribute("happy", nil, core.MakeBoolLiteralValue(true, nil)))
		structFoo.AddField(core.NewStructField(core.DeclTestDataAWithOrdinal("x", attributes, 0), core.SimpleTypeInt32, nil))
		expectedFile.AddStruct(structFoo)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	module core.test;

	import "another.file";
	import "and.another.file";

	struct Foo{
		int32 x@0 = 42;
		[age=7, level="high"] string  y = "Howdy!";
		string? z;
		bool w@3 = false;
	};`
	{
		expectedFile.AddImport(core.NewImportedFile("another.file", nil))
		expectedFile.AddImport(core.NewImportedFile("and.another.file", nil))

		structFoo := core.NewMojomStruct(core.DeclTestData("Foo"))
		structFoo.InitAsScope(core.NewTestFileScope("test.scope"))
		structFoo.AddField(core.NewStructField(core.DeclTestDataWithOrdinal("x", 0), core.SimpleTypeInt32, core.MakeInt8LiteralValue(42, nil)))
		attributes := core.NewAttributes(lexer.Token{})
		attributes.List = append(attributes.List, core.NewMojomAttribute("age", nil, core.MakeInt8LiteralValue(7, nil)))
		attributes.List = append(attributes.List, core.NewMojomAttribute("level", nil, core.MakeStringLiteralValue("high", nil)))
		structFoo.AddField(core.NewStructField(core.DeclTestDataA("y", attributes), core.BuiltInType("string"), core.MakeStringLiteralValue("Howdy!", nil)))
		structFoo.AddField(core.NewStructField(core.DeclTestData("z"), core.BuiltInType("string?"), nil))
		structFoo.AddField(core.NewStructField(core.DeclTestDataWithOrdinal("w", 3), core.BuiltInType("bool"), core.MakeBoolLiteralValue(false, nil)))
		expectedFile.AddStruct(structFoo)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	module core.test;

	import "another.file";
	import "and.another.file";

	struct Foo{
		int32 x;
		string  y;
		string? z;
	};

	interface Doer {
		DoIt(int8 lemon, handle<message_pipe> pipe) => (array<Foo> someFoos, Foo? anotherFoo);
	};

	`
	{
		expectedFile.AddImport(core.NewImportedFile("another.file", nil))
		expectedFile.AddImport(core.NewImportedFile("and.another.file", nil))

		structFoo := core.NewMojomStruct(core.DeclTestData("Foo"))
		structFoo.InitAsScope(core.NewTestFileScope("test.scope"))
		structFoo.AddField(core.NewStructField(core.DeclTestData("x"), core.SimpleTypeInt32, nil))
		structFoo.AddField(core.NewStructField(core.DeclTestData("y"), core.BuiltInType("string"), nil))
		structFoo.AddField(core.NewStructField(core.DeclTestData("z"), core.BuiltInType("string?"), nil))
		expectedFile.AddStruct(structFoo)

		interfaceDoer := core.NewMojomInterface(core.DeclTestData("Doer"))
		interfaceDoer.InitAsScope(core.NewTestFileScope("test.scope"))

		// The first reference to Foo inside of interface Doer
		fooRef1 := core.NewUserTypeRef("Foo", false, false, interfaceDoer.Scope(), lexer.Token{})

		// The second reference to Foo inside of interface Doer. nullable=true
		fooRef2 := core.NewUserTypeRef("Foo", true, false, interfaceDoer.Scope(), lexer.Token{})

		params := core.NewMojomStruct(core.DeclTestData("dummy"))
		params.InitAsScope(core.NewTestFileScope("test.scope"))
		params.AddField(core.NewStructField(core.DeclTestData("lemon"), core.SimpleTypeInt8, nil))
		params.AddField(core.NewStructField(core.DeclTestData("pipe"), core.BuiltInType("handle<message_pipe>"), nil))
		responseParams := core.NewMojomStruct(core.DeclTestData("dummy"))
		responseParams.InitAsScope(core.NewTestFileScope("test.scope"))
		responseParams.AddField(core.NewStructField(core.DeclTestData("someFoos"), core.NewArrayTypeRef(fooRef1, -1, false), nil))
		responseParams.AddField(core.NewStructField(core.DeclTestData("anotherFoo"), fooRef2, nil))

		interfaceDoer.AddMethod(core.NewMojomMethod(core.DeclTestData("DoIt"), params, responseParams))
		expectedFile.AddInterface(interfaceDoer)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Annotation right after imports)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
    import "gpu/interfaces/command_buffer.mojom";

    [ServiceName="mojo::Gpu"]
    interface Gpu {
    };

	`
	{
		expectedFile.AddImport(core.NewImportedFile("gpu/interfaces/command_buffer.mojom", nil))

		attributes := core.NewAttributes(lexer.Token{})
		attributes.List = append(attributes.List, core.NewMojomAttribute("ServiceName", nil, core.MakeStringLiteralValue("mojo::Gpu", nil)))
		interfaceGpu := core.NewMojomInterface(core.DeclTestDataA("Gpu", attributes))
		expectedFile.AddInterface(interfaceGpu)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case
	////////////////////////////////////////////////////////////
	startTestCase("core.test")
	cases[testCaseNum].mojomContents = `
	[php_namespace="core.test.php"]
	module core.test;

	import "another.file";
	import "and.another.file";

	const int8 TOO_SMALL_VALUE = 6;

	enum ErrorCodes {
		TOO_BIG = 5,
		TOO_SMALL = TOO_SMALL_VALUE,
		JUST_RIGHT,
	};

	struct Foo{
		int32 x;
		string  y;
		string? z;
	};

	interface Doer {
		DoIt(int8 lemon, handle<message_pipe> pipe) => (array<Foo> someFoos, Foo? anotherFoo);
	};

	`
	{
		expectedFile.Attributes = core.NewAttributes(lexer.Token{})
		expectedFile.Attributes.List = append(expectedFile.Attributes.List,
			core.NewMojomAttribute("php_namespace", nil, core.MakeStringLiteralValue("core.test.php", nil)))

		expectedFile.AddImport(core.NewImportedFile("another.file", nil))
		expectedFile.AddImport(core.NewImportedFile("and.another.file", nil))

		expectedFile.AddConstant(core.NewUserDefinedConstant(core.DeclTestData("TOO_SMALL_VALUE"),
			core.SimpleTypeInt8, core.MakeInt8LiteralValue(6, nil)))

		errorCodeEnum := core.NewMojomEnum(core.DeclTestData("ErrorCodes"))
		errorCodeEnum.InitAsScope(expectedFile.FileScope)

		// The reference to TOO_SMALL_VALUE from within the ErrorCodes enum.
		assigneeType := core.NewResolvedUserTypeRef("ErrorCodes", errorCodeEnum)
		tooSmallValueRef := core.NewUserValueRef(core.AssigneeSpec{"assignee", assigneeType}, "TOO_SMALL_VALUE",
			expectedFile.FileScope, lexer.Token{})

		errorCodeEnum.AddEnumValue(core.DeclTestData("TOO_BIG"), core.MakeInt8LiteralValue(5, nil))
		errorCodeEnum.AddEnumValue(core.DeclTestData("TOO_SMALL"), tooSmallValueRef)
		errorCodeEnum.AddEnumValue(core.DeclTestData("JUST_RIGHT"), nil)
		expectedFile.AddEnum(errorCodeEnum)

		structFoo := core.NewMojomStruct(core.DeclTestData("Foo"))
		structFoo.InitAsScope(core.NewTestFileScope("test.scope"))
		structFoo.AddField(core.NewStructField(core.DeclTestData("x"), core.SimpleTypeInt32, nil))
		structFoo.AddField(core.NewStructField(core.DeclTestData("y"), core.BuiltInType("string"), nil))
		structFoo.AddField(core.NewStructField(core.DeclTestData("z"), core.BuiltInType("string?"), nil))
		expectedFile.AddStruct(structFoo)

		interfaceDoer := core.NewMojomInterface(core.DeclTestData("Doer"))
		interfaceDoer.InitAsScope(core.NewTestFileScope("test.scope"))

		// The first reference to Foo inside of interface Doer
		fooRef1 := core.NewUserTypeRef("Foo", false, false, interfaceDoer.Scope(), lexer.Token{})

		// The second reference to Foo inside of interface Doer. nullable=true
		fooRef2 := core.NewUserTypeRef("Foo", true, false, interfaceDoer.Scope(), lexer.Token{})

		params := core.NewMojomStruct(core.DeclTestData("dummy"))
		params.InitAsScope(core.NewTestFileScope("test.scope"))
		params.AddField(core.NewStructField(core.DeclTestData("lemon"), core.SimpleTypeInt8, nil))
		params.AddField(core.NewStructField(core.DeclTestData("pipe"), core.BuiltInType("handle<message_pipe>"), nil))
		responseParams := core.NewMojomStruct(core.DeclTestData("dummy"))
		responseParams.InitAsScope(core.NewTestFileScope("test.scope"))
		responseParams.AddField(core.NewStructField(core.DeclTestData("someFoos"), core.NewArrayTypeRef(fooRef1, -1, false), nil))
		responseParams.AddField(core.NewStructField(core.DeclTestData("anotherFoo"), fooRef2, nil))

		interfaceDoer.AddMethod(core.NewMojomMethod(core.DeclTestData("DoIt"), params, responseParams))
		expectedFile.AddInterface(interfaceDoer)
	}
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for _, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
		} else {
			got := parser.GetMojomFile().String()
			expected := c.expectedFile.String()
			if got != expected {
				t.Errorf("%s:\n*****expected:\n%s\n****actual\n%s", c.fileName, expected, got)
			}
		}
	}
}

// TestErrorParsing contains a series of test cases in which we
// run the parser on invalid mojom input string and compare the resulting
// error message to an expected one.
func TestErrorParsing(t *testing.T) {
	type testCase struct {
		fileName       string
		mojomContents  string
		expectedErrors []string
	}
	cases := make([]testCase, 0)
	testCaseNum := 0

	startTestCase := func(moduleNameSpace string) {
		fileName := fmt.Sprintf("file%d", testCaseNum)
		cases = append(cases, testCase{fileName: fileName})
	}

	expectError := func(expectedError string) {
		cases[testCaseNum].expectedErrors = append(cases[testCaseNum].expectedErrors, expectedError)
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	// Note(rudominer) The structure of this method is designed to allow
	// test cases to be rearranged and new test cases to be inserted at
	// arbitrary locations. Do not hard-code anything that refers to the
	// position of a test case in the list.

	////////////////////////////////////////////////////////////
	// Test Case (naked attributes)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = "[cool=true]"
	expectError("The .mojom file contains an attributes section but nothing else.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (attributes directly before an import)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	[cool=true]
	import "another.file";
	`
	expectError("Attributes are not allowed before an import statement.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (two sets of initial naked attributes)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	[cool=true]
	[not-cool=false]
	`
	expectError("Unexpected ")
	expectError("'['")
	expectError("Expecting module, import, interface, struct, union, enum or constant.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (two sets of initial attributes with a module)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	[cool=true]
	[not-cool=false]
	module core.test;
	`
	expectError("Unexpected ")
	expectError("'['")
	expectError("Expecting module, import, interface, struct, union, enum or constant.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (import before module)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	import "another.file";

	module core.test;
	`
	expectError("The module declaration must come before the import statements.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: empty string
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@;
	};

	`
	expectError("field \"x\": Invalid ordinal string following '@'")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: Not a number
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@happy;
	};

	`
	expectError("field \"x\": Invalid ordinal string following '@'")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: Negative
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@-500;
	};

	`
	expectError("field \"x\": Invalid ordinal string following '@'")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: too big for uint32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@4294967295;
	};

	`
	expectError("field \"x\": Invalid ordinal string following '@'")
	expectError("4294967295")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: too big for uint64)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@999999999999999999999999999999999999999;
	};

	`
	expectError("field \"x\": Invalid ordinal string following '@'")
	expectError("999999999999999999999999999999999999999")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: too big for size of struct)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x;
		int32 y@2;
	};

	`
	expectError("Invalid ordinal for field y: 2.")
	expectError("A struct field ordinal must be a non-negative integer value less than the number of fields in the struct.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: implicit next value too big for size of struct)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x;
		int32 y@2;
		int32 z;
	};

	`
	expectError("Invalid ordinal for field z: 3.")
	expectError("A struct field ordinal must be a non-negative integer value less than the number of fields in the struct.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: Duplicate explicit)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x@0;
		int32 y;
		int32 z@0;
	};

	`
	expectError("Invalid ordinal for field z: 0.")
	expectError("There is already a field in struct MyStruct with that ordinal: x")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid struct field ordinal: Duplicate implicit)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		int32 x;
		int32 y;
		int32 z@1;
	};

	`
	expectError("Invalid ordinal for field z: 1.")
	expectError("There is already a field in struct MyStruct with that ordinal: y")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid union field ordinal: too big for uint32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	union MyUnion {
		int8  x;
		int16 y@4294967295;
		int32 z;
	};

	`
	expectError("union field \"y\": Invalid ordinal string following '@'")
	expectError("4294967295")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid union field ordinal: implicit next value too big for uint32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	union MyUnion {
		int8  x;
		int16 y@4294967294;
		int32 z;
	};
	`
	expectError("Invalid tag for field z")
	expectError("4294967295")
	expectError("A union field tag must be between 0 and 4,294,967,294.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid union field ordinal: Duplicate explicit)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	union MyUnion {
		int8  x@0;
		int16 y;
		int32 z@0;
	};
	`
	expectError("Invalid tag for field z: 0.")
	expectError("There is already a field in union MyUnion with that tag: x")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid union field ordinal: Duplicate implicit)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	union MyUnion {
		int8  x;
		int16 y;
		int32 z@1;
	};
	`
	expectError("Invalid tag for field z: 1.")
	expectError("There is already a field in union MyUnion with that tag: y")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid method ordinal: too big for uint32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		MethodA@4294967295();
	};

	`
	expectError("MethodA")
	expectError("4294967295")
	expectError("Invalid ordinal string")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid method ordinal: too big for int64)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		MethodA@999999999999999999999999999999999999999();
	};

	`
	expectError("MethodA")
	expectError("999999999999999999999999999999999999999")
	expectError("Invalid ordinal string")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Invalid method ordinal: negative)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		MethodA@-1();
	};

	`
	expectError("MethodA")
	// Note that the lexer return "@" as the ordinal token, stopping when
	// it sees the non-digit "-".
	expectError("Invalid ordinal string")
	expectError("Ordinals must be decimal integers between 0 and 4294967294")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Constant integer too big for uint64)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const uint64 manyNines = 99999999999999999999;
	`
	expectError("Integer literal value out of range")
	expectError("99999999999999999999")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Constant float too big for double)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const uint64 veryBig = 3.14159E400;
	`
	expectError("Float literal value out of range: 3.14159E400")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Use array as constant type)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const array<uint64> Foo = 0;
	`
	expectError("The type array<uint64> is not allowed as the type of a declared constant.")
	expectError("Only simple types, strings and enum types may be the types of constants.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Identifier ends with a dot)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const array<my.Foo.Type.> Foo = 0;
	`
	expectError("Invalid identifier")
	expectError("\"my.Foo.Type.\"")
	expectError("Identifiers may not end with a dot")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Unrecognized type of handle)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		handle<drawer> x;
	};
	`
	expectError("Unrecognized type of handle: handle<drawer>")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Nullable bool)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		bool? x;
	};
	`
	expectError("The type bool? is invalid because the type bool may not be made nullable")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if parser.OK() {
			t.Errorf("Parsing was supposed to fail but did not for test case %d", i)
		} else {
			got := parser.GetError().Error()
			for _, expected := range c.expectedErrors {
				if !strings.Contains(got, expected) {
					t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
				}
			}
		}
	}
}

// TestInvalidAssignmentDuringParsing contains a series of test cases in which we
// run the parser on invalid mojom input string and compare the resulting
// error message to an expected one. The particular type of error we are testing
// here is invalid assignments of values to variables that may be detected during
// parsing.
func TestInvalidAssignmentDuringParsing(t *testing.T) {
	type testCase struct {
		fileName       string
		mojomContents  string
		expectedErrors []string
	}
	cases := make([]testCase, 0)
	testCaseNum := 0

	startTestCase := func(moduleNameSpace string) {
		fileName := fmt.Sprintf("file%d", testCaseNum)
		cases = append(cases, testCase{fileName: fileName})
	}

	expectError := func(expectedError string) {
		cases[testCaseNum].expectedErrors = append(cases[testCaseNum].expectedErrors, expectedError)
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	////////////////////////////////////////////////////////////
	// Group 1: Assign to struct field default value.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case (Assign string to int32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		int32 x = "hello";
	};

	`
	expectError("Illegal assignment")
	expectError("Field x of type int32 may not be assigned the value \"hello\" of type string.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign int32 to string)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		string? x = 42;
	};

	`
	expectError("Illegal assignment")
	expectError("Field x of type string? may not be assigned the value 42 of type int8.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign negative number to unit8)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		uint8 x = -1;
	};

	`
	expectError("Illegal assignment")
	expectError("Field x of type uint8 may not be assigned the value -1 of type int8.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign large integer to unit8)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		uint8 x = 9999999999;
	};

	`
	expectError("Illegal assignment")
	expectError("Field x of type uint8 may not be assigned the value 9999999999 of type int64.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign large float to float32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		float x = 3.14159E40;
	};
	`
	expectError("Illegal assignment")
	expectError("Field x of type float may not be assigned the value 3.14159e+40 of type double.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign default keyword to string)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		string x = default;
	};

	`
	expectError("Illegal assignment")
	expectError("The 'default' keyword may not be used with the field x of type string")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign integer to array field)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
	};

	struct Bar {
		array<Foo?, 8>? x = 7;
	};
	`
	expectError("Illegal assignment")
	expectError("Field x of type array<Foo?, 8>? may not be assigned the value 7 of type int8.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign default keyword to map field)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
	};

	struct Bar {
		map<bool, Foo?>? x = default;
	};
	`
	expectError("Illegal assignment")
	expectError("The 'default' keyword may not be used with the field x of type map<bool, Foo?>?.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Group 2: Assign to constant.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case (Assign boolean to int32)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const int32 Foo = true;
	`
	expectError("Illegal assignment")
	expectError("Constant Foo of type int32 may not be assigned the value true of type bool.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (Assign string to bool)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	const bool Foo = "true";
	`
	expectError("Illegal assignment")
	expectError("Constant Foo of type bool may not be assigned the value \"true\" of type string.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if parser.OK() {
			t.Errorf("Parsing was supposed to fail but did not for test case %d", i)
		} else {
			got := parser.GetError().Error()
			for _, expected := range c.expectedErrors {
				if !strings.Contains(got, expected) {
					t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
				}
			}
		}
	}
}

// TestLexerErrors contains a series of test cases in which we
// run the parser on invalid mojom input string and compare the resulting
// error message to an expected one. The particular type of error we are testing
// here are cases in which the lexer detects an error and returns one of
// its error tokens.
func TestLexerErrors(t *testing.T) {
	type testCase struct {
		fileName       string
		mojomContents  string
		expectedErrors []string
	}
	cases := make([]testCase, 0)
	testCaseNum := 0

	startTestCase := func(moduleNameSpace string) {
		fileName := fmt.Sprintf("file%d", testCaseNum)
		cases = append(cases, testCase{fileName: fileName})
	}

	expectError := func(expectedError string) {
		cases[testCaseNum].expectedErrors = append(cases[testCaseNum].expectedErrors, expectedError)
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	////////////////////////////////////////////////////////////
	// Group 1: Unterminated comment
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Unterminated comment at start of file.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	/*
	* The woods are lovely
	* dark and deep
	* but I have promises to keep.
	struct Foo {
		int32 x = "hello";
	};
	`
	expectError("Error")
	expectError("unterminated comment")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case: Unterminated comment at end of file.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		int32 x ;
	};

	/*
	* The woods are lovely
	* dark and deep
	* but I have promises to keep.
	`
	expectError("Error")
	expectError("unterminated comment")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case: Unterminated comment in the middle.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		int32 x ;
		/*
	     * The woods are lovely
	     * dark and deep
	     * but I have promises to keep.
	};`
	expectError("Error")
	expectError("unterminated comment")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Group 2: Unterminated string literal
	////////////////////////////////////////////////////////////

	/// ////////////////////////////////////////////////////////////
	// Test Case: Unterminated string literal in import.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
    import "foo.bar
	struct Foo {
		int32 x = 42;
	};
	`
	expectError("Error")
	expectError("unterminated string literal")
	endTestCase()

	/// ////////////////////////////////////////////////////////////
	// Test Case: Unterminated string literal in assignment.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
    import "foo.bar";
	struct Foo {
		string x = "hello;
	};
	`
	expectError("Error")
	expectError("unterminated string literal")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Group 3: ErrorIllegalChar
	////////////////////////////////////////////////////////////

	/// ////////////////////////////////////////////////////////////
	// Test Case: ErrorIllegalChar at the beginning of  a file.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	/? What the ?/
    import "foo.bar"
	struct Foo {
		int32 x = 42;
	};
	`
	expectError("Error:")
	expectError("Unexpected \"/\"")
	endTestCase()

	/// ////////////////////////////////////////////////////////////
	// Test Case: ErrorIllegalChar in the middle.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
    import "foo.bar";
	struct Foo {
		int32 x = %42;
	};
	`
	expectError("Error:")
	expectError("Unexpected \"%\"")
	endTestCase()

	/// ////////////////////////////////////////////////////////////
	// Test Case: ErrorIllegalChar at the end
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
    import "foo.bar";
	struct Foo {
		int32 x = 42;
	};
	*
	`
	expectError("Error:")
	expectError("Unexpected \"*\"")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if parser.OK() {
			t.Errorf("Parsing was supposed to fail but did not for test case %d", i)
		} else {
			got := parser.GetError().Error()
			for _, expected := range c.expectedErrors {
				if !strings.Contains(got, expected) {
					t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
				}
			}
		}
	}
}

func TestDuplicateNameErrorsSingleFile(t *testing.T) {
	type testCase struct {
		fileName       string
		mojomContents  string
		expectedErrors []string
	}
	cases := make([]testCase, 0)
	testCaseNum := 0

	startTestCase := func(moduleNameSpace string) {
		fileName := fmt.Sprintf("file%d", testCaseNum)
		cases = append(cases, testCase{fileName: fileName})
	}

	expectError := func(expectedError string) {
		cases[testCaseNum].expectedErrors = append(cases[testCaseNum].expectedErrors, expectedError)
	}

	fileName := func() string {
		return cases[testCaseNum].fileName
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	////////////////////////////////////////////////////////////
	// Test Case: Duplicate struct field name.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct Foo {
		int32 x = 3;
		string y = "hello";
		float x = 1.7;
	};
	`
	expectError("Error")
	expectError("Duplicate definition of 'x'.")
	expectError("There is already a field with that name in struct Foo.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case: Duplicate union field name.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	union Foo {
		int32 x;
		string y;
		float x ;
	};
	`
	expectError("Error")
	expectError("Duplicate definition of 'x'.")
	expectError("There is already a field with that name in union Foo.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case: Duplicate method request parameter name.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface Foo {
		DoIt(int32 x, string y, float x);
	};
	`
	expectError("Error")
	expectError("Duplicate definition of 'x'.")
	expectError("There is already a request parameter with that name in method DoIt.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case: Duplicate method response parameter name.
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface Foo {
		DoIt() => (int32 x, string y, float x);
	};
	`
	expectError("Error")
	expectError("Duplicate definition of 'x'.")
	expectError("There is already a response parameter with that name in method DoIt.")
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (duplicate method names)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		MethodA();
		MethodB();
		MethodC();
		MethodB();
		MethodD();
	};
	`
	expectError("Duplicate definition for \"MethodB\". Previous definition with the same fully-qualified name:")
	expectError("method MyInterface.MethodB at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (A method and an enum with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		Foo();
		Bar();
		enum Foo{
			PLAID = 1,
			CHECKERED = 2
		};
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("method MyInterface.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (An enum and a method with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		Bar();
		enum Foo{
			PLAID = 1,
			CHECKERED = 2
		};
		Foo();
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("enum MyInterface.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (A method and a constant with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		Foo();
		Bar();
		const int32 Foo = 7;
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("method MyInterface.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (A constant and a method with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	interface MyInterface {
		const int32 Foo = 7;
		Foo();
		Bar();
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("const MyInterface.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (A constant and a field with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		const int32 Bar = 7;
		string Foo;
		int32 Bar;
	};
	`
	expectError("Duplicate definition for \"Bar\". Previous definition with the same fully-qualified name:")
	expectError("const MyStruct.Bar at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (A field and a constant with the same name)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	struct MyStruct {
		string Foo;
		int32 Bar;
		const int32 Bar = 7;
	};
	`
	expectError("Duplicate definition for \"Bar\". Previous definition with the same fully-qualified name:")
	expectError("field MyStruct.Bar at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (two types with the same name in file scope)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	module a.b.c;

	interface Foo {
	};

	struct Bar {
	};

	struct Foo {
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("interface a.b.c.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (two values with the same name in file scope)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	module a.b.c;

	const int32 NUM_HATS = 6;
	const string NUM_HATS = "6";
	`
	expectError("Duplicate definition for \"NUM_HATS\". Previous definition with the same fully-qualified name:")
	expectError("const a.b.c.NUM_HATS at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (a value with the same name as a type at file scope)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	module a.b.c;

	union Foo {
	};

	const int32 Foo = 42;
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("union a.b.c.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (a type with the same name as a value)
	////////////////////////////////////////////////////////////
	startTestCase("")
	cases[testCaseNum].mojomContents = `
	module a.b.c;

	const int32 Foo = 42;

	union Foo {
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("const a.b.c.Foo at " + fileName())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.fileName, c.fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if parser.OK() {
			t.Errorf("Parsing was supposed to fail but did not for test case %d", i)
		} else {
			got := parser.GetError().Error()
			for _, expected := range c.expectedErrors {
				if !strings.Contains(got, expected) {
					t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
				}
			}
		}
	}
}

func TestDuplicateNameErrorsTwoFiles(t *testing.T) {
	type testFile struct {
		fileName      string
		mojomContents string
	}

	type testCase struct {
		file1, file2   testFile
		expectedErrors []string
	}
	cases := make([]testCase, 0)
	testCaseNum := 0

	startTestCase := func() {
		fileNameA := fmt.Sprintf("file%dA", testCaseNum)
		fileNameB := fmt.Sprintf("file%dB", testCaseNum)
		cases = append(cases, testCase{
			file1: testFile{fileName: fileNameA},
			file2: testFile{fileName: fileNameB}})
	}

	expectError := func(expectedError string) {
		cases[testCaseNum].expectedErrors = append(cases[testCaseNum].expectedErrors, expectedError)
	}

	fileName1 := func() string {
		return cases[testCaseNum].file1.fileName
	}

	endTestCase := func() {
		testCaseNum += 1
	}

	////////////////////////////////////////////////////////////
	// Test Case (two types with the same name in different scopes)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b.c;

	struct Foo {
	};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b;

	struct c {
		enum Foo{};
	};
	`
	expectError("Duplicate definition for \"Foo\". Previous definition with the same fully-qualified name:")
	expectError("struct a.b.c.Foo at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (two vales with the same name in different scopes)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b.c;

	enum Hats {
		COWBOY = 1,
		TOP
	};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b.c.Hats;

	const double TOP = 4.9;
	`
	expectError("Duplicate definition for \"TOP\". Previous definition with the same fully-qualified name:")
	expectError("enum value a.b.c.Hats.TOP at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (a value with the same name as a type in a different scope)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b.c;

	interface Hats{};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b.c;

	const double Hats = 4.9;
	`
	expectError("Duplicate definition for \"Hats\". Previous definition with the same fully-qualified name:")
	expectError("interface a.b.c.Hats at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (another value with the same name as a type in a different scope)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b.c.Hats;

	interface COWBOY{};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b.c;

	enum Hats {
		COWBOY = 1,
		TOP
	};
	`
	expectError("Duplicate definition for \"COWBOY\". Previous definition with the same fully-qualified name:")
	expectError("interface a.b.c.Hats.COWBOY at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (a type with the same name as a value in a different scope)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b.c;

	enum Hats {
		COWBOY = 1,
		TOP
	};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b.c.Hats;

	interface COWBOY{};
	`
	expectError("Duplicate definition for \"COWBOY\". Previous definition with the same fully-qualified name:")
	expectError("enum value a.b.c.Hats.COWBOY at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Test Case (a value with the same name as a method in a different scope)
	////////////////////////////////////////////////////////////
	startTestCase()
	cases[testCaseNum].file1.mojomContents = `
	module a.b;

	interface Big {
		Run();
	};
	`

	cases[testCaseNum].file2.mojomContents = `
	module a.b.Big;

	const int32 Run = 0;
	`
	expectError("Duplicate definition for \"Run\". Previous definition with the same fully-qualified name:")
	expectError("method a.b.Big.Run at " + fileName1())
	endTestCase()

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range cases {
		descriptor := core.NewMojomDescriptor()
		parser := MakeParser(c.file1.fileName, c.file1.fileName, c.file1.mojomContents, descriptor, nil)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing was supposed to succeed for file1 but did not for test case %d: %s", i, parser.GetError().Error())
		}
		parser = MakeParser(c.file2.fileName, c.file2.fileName, c.file2.mojomContents, descriptor, nil)
		parser.Parse()
		if parser.OK() {
			t.Errorf("Parsing was supposed to fail for file2 but did not for test case %d", i)
		} else {
			got := parser.GetError().Error()
			for _, expected := range c.expectedErrors {
				if !strings.Contains(got, expected) {
					t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.file2.fileName, expected, got)
				}
			}
		}
	}
}

// TestLexicalOrdering tests that the DeclaredObjects fields of the
// components of the mojom file are in order of occurrence in the source.
func TestLexicalOrdering(t *testing.T) {
	source := `
	[Key1="SomeModule",
	Key2=10]
	module hello.world;

	import "import1";
	import "import2";

	interface InterfaceFoo {
		Method1();
		enum InnerEnum { };
		Method2();
		const int8 inner_const = 10;
		Method3();
	};

	struct StructFoo {
		int8 field1;
		enum InnerEnum { };
		int8 field2;
		const int8 inner_const = 10;
		int8 field3;
	};

	union UnionFoo {
		int8 field1;
		int8 field2;
	};

	enum EnumFoo {
		VALUE1,
		VALUE2,
	};

	const int8 const_foo = 10;
	`

	descriptor := core.NewMojomDescriptor()
	parser := MakeParser("filename", "filename", source, descriptor, nil)
	parser.Parse()

	if !parser.OK() {
		t.Errorf("Parser was not supposed to fail: %v", parser.GetError().Error())
	}

	checkEq := func(expected, actual interface{}) {
		if expected != actual {
			t.Fatalf("Failed check: Expected (%v), Actual (%v)", expected, actual)
		}
	}

	mojomFile := parser.GetMojomFile()

	checkEq("Key1", mojomFile.Attributes.List[0].Key)
	checkEq("Key1", mojomFile.Attributes.List[0].KeyToken.Text)
	checkEq("Key2", mojomFile.Attributes.List[1].Key)
	checkEq("Key2", mojomFile.Attributes.List[1].KeyToken.Text)

	checkEq("import1", mojomFile.Imports[0].SpecifiedName)
	checkEq("import2", mojomFile.Imports[1].SpecifiedName)

	mojomInterface := mojomFile.DeclaredObjects[0].(*core.MojomInterface)
	checkEq("InterfaceFoo", mojomInterface.SimpleName())
	checkEq("Method1", mojomInterface.DeclaredObjects[0].(*core.MojomMethod).SimpleName())
	checkEq("InnerEnum", mojomInterface.DeclaredObjects[1].(*core.MojomEnum).SimpleName())
	checkEq("Method2", mojomInterface.DeclaredObjects[2].(*core.MojomMethod).SimpleName())
	checkEq("inner_const", mojomInterface.DeclaredObjects[3].(*core.UserDefinedConstant).SimpleName())
	checkEq("Method3", mojomInterface.DeclaredObjects[4].(*core.MojomMethod).SimpleName())

	mojomStruct := mojomFile.DeclaredObjects[1].(*core.MojomStruct)
	checkEq("StructFoo", mojomStruct.SimpleName())
	checkEq("field1", mojomStruct.DeclaredObjects[0].(*core.StructField).SimpleName())
	checkEq("InnerEnum", mojomStruct.DeclaredObjects[1].(*core.MojomEnum).SimpleName())
	checkEq("field2", mojomStruct.DeclaredObjects[2].(*core.StructField).SimpleName())
	checkEq("inner_const", mojomStruct.DeclaredObjects[3].(*core.UserDefinedConstant).SimpleName())
	checkEq("field3", mojomStruct.DeclaredObjects[4].(*core.StructField).SimpleName())

	mojomUnion := mojomFile.DeclaredObjects[2].(*core.MojomUnion)
	checkEq("UnionFoo", mojomUnion.SimpleName())
	checkEq("field1", mojomUnion.DeclaredObjects[0].(*core.UnionField).SimpleName())
	checkEq("field2", mojomUnion.DeclaredObjects[1].(*core.UnionField).SimpleName())

	mojomEnum := mojomFile.DeclaredObjects[3].(*core.MojomEnum)
	checkEq("EnumFoo", mojomEnum.SimpleName())
	checkEq("VALUE1", mojomEnum.DeclaredObjects[0].(*core.EnumValue).SimpleName())
	checkEq("VALUE2", mojomEnum.DeclaredObjects[1].(*core.EnumValue).SimpleName())

	checkEq("const_foo", mojomFile.DeclaredObjects[4].(*core.UserDefinedConstant).SimpleName())
}
