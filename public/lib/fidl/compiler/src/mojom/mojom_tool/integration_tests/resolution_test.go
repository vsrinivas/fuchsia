// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"mojom/mojom_tool/mojom"
	"mojom/mojom_tool/parser"
	"strings"
	"testing"
)

type singleFileTestCase struct {
	fileName       string
	mojomContents  string
	importedFrom   *mojom.MojomFile
	expectedErrors []string
}

type singleFileTest struct {
	cases       []singleFileTestCase
	testCaseNum int
}

func (test *singleFileTest) addTestCase(contents string, expectedErrors []string) {
	fileName := fmt.Sprintf("file%d", test.testCaseNum)
	var importedFrom *mojom.MojomFile = nil
	test.cases = append(test.cases, singleFileTestCase{fileName, contents, importedFrom, expectedErrors})
	test.testCaseNum += 1
}

// TestSingleFileResolutionErrors() tests that appropriate error messages are generated
// when a .mojom file contains unresolved references.
func TestSingleFileResolutionErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: One unresolved value reference
	////////////////////////////////////////////////////////////
	{
		contents := `

    struct Foo{
      int32 x = Bar;
    };`

		test.addTestCase(contents, []string{
			"Undefined value: \"Bar\"",
			"int32 x = Bar;",
			"^^^"})
	}

	////////////////////////////////////////////////////////////
	// Test Case:Two unresolved value references
	////////////////////////////////////////////////////////////
	{
		contents := `

    const bool F = Baz;

    struct Foo{
      int32 x = Bar;
    };`

		test.addTestCase(contents, []string{
			"Undefined value: \"Baz\"", "Undefined value: \"Bar\""})
	}

	////////////////////////////////////////////////////////////
	// Test Case: One unresolved type reference
	////////////////////////////////////////////////////////////
	{
		contents := `

    struct Foo{
      Bar x;
    };`

		test.addTestCase(contents, []string{"Undefined type: \"Bar\""})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Multiple unresolved types and values.
	////////////////////////////////////////////////////////////
	{
		contents := `

	const int32 X = Boom;

    struct Foo{
      Bar x;
      Baz y = Bing;
      int32 z = X;
    };

    struct Foo2 {
    	Foo f = default;
    };`

		test.addTestCase(contents, []string{
			"Undefined type: \"Bar\"", "Undefined type: \"Baz\"",
			"Undefined value: \"Boom\"", "Undefined value: \"Bing\"",
			"Use of unresolved value: \"X\"",
			"^^^^"}) // There will be four carets in the snippet because of "Boom"
	}

	////////////////////////////////////////////////////////////
	// Test Case: Circular reference of constants
	////////////////////////////////////////////////////////////
	{
		contents := `
		const int32 MyConst1 = MyConst2;
		const int32 MyConst2 = MyConst3;
		const int32 MyConst3 = MyConst1;`

		test.addTestCase(contents, []string{
			"Use of unresolved value: \"MyConst2\"",
			"Use of unresolved value: \"MyConst3\"",
			"Use of unresolved value: \"MyConst1\"",
		})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := mojom.NewMojomDescriptor()
		specifiedName := ""
		if c.importedFrom == nil {
			specifiedName = c.fileName
		}
		parser := parser.MakeParser(c.fileName, specifiedName, c.mojomContents, descriptor, c.importedFrom)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		err := descriptor.Resolve()
		if err == nil {
			t.Errorf("Resolution unexpectedly succeeded for test case %d.", i)
			continue
		}
		got := err.Error()
		for _, expected := range c.expectedErrors {
			if !strings.Contains(got, expected) {
				t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
			}
		}

	}
}

// TestSingleFileValueValidationErrors() test the function
// UserValueRef.validateAfterResolution(). It tests that appropriate error messages are generated
// when a .mojom file contains validation errors. In particular we are testing errors
// that are not detected during parsing but are only detected after all names have
// been resolved. Thus we are not testing here, for example, an attempt to assign
// an int32 literal value to a string variable, because that type of error can be
// detected during parsing. But we are testing the case of a constant whose value is
// an int32 literal being assigned to a string variable.
func TestSingleFileValueValidationErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Group 1: The left-hand-side is an int32 variable.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of bool type to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const bool Bar = true;

	struct Foo{
	  int32 x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value true of type bool may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of string type to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const string Bar = "Hi Bar";

	struct Foo{
	  int32 x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value \"Hi Bar\" of type string may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant with value double.INFINITY  to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const double Bar = double.INFINITY;

	struct Foo{
	  int32 x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value double.INFINITY may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign double.INFINITY directly to an int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x = double.INFINITY;
	};`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"double.INFINITY may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of enum value to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = TWO;

	struct Foo{
	  int32 x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value MyEnum.TWO may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign enum value directly to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const int32 Bar = MyEnum.TWO;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The enum value MyEnum.TWO of type MyEnum may not be assigned to Bar of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Group 2: The left-hand-side is a string variable.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of int32 type to string variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const int32 Bar = 42;

	struct Foo{
	  string? x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value 42 of type int8 may not be assigned to x of type string?"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant with value double.INFINITY to a string variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const double Bar = double.INFINITY;

	struct Foo{
	  string x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value double.INFINITY may not be assigned to x of type string"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign double.INFINITY directly to a string variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const string Bar = double.INFINITY;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"double.INFINITY may not be assigned to Bar of type string"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of enum value to string variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = TWO;

	struct Foo{
	  string x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value MyEnum.TWO may not be assigned to x of type string"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign enum value directly to a string variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const string Bar = MyEnum.TWO;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The enum value MyEnum.TWO of type MyEnum may not be assigned to Bar of type string"})
	}

	////////////////////////////////////////////////////////////
	// Group 3: The left-hand-side is an enum variable.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of bool type to an enum variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const bool Bar = true;

	enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };


	struct Foo{
	  MyEnum x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value true of type bool may not be assigned to x of type MyEnum"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of value double.INFINITY to an enum variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const double Bar = double.INFINITY;

	struct Foo{
	  MyEnum x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value double.INFINITY may not be assigned to x of type MyEnum"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign double.INFINITY directly to an enum variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = double.INFINITY;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"double.INFINITY may not be assigned to Bar of type MyEnum"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of enum value to a variable of a different enum type.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	  enum MyOtherEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = TWO;

	struct Foo{
	  MyOtherEnum x = Bar;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value MyEnum.TWO may not be assigned to x of type MyOtherEnum"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign enum value to a variable of a different enum type.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	  enum MyOtherEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = MyOtherEnum.TWO;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The enum value MyOtherEnum.TWO of type MyOtherEnum may not be assigned to Bar of type MyEnum"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Use an enum value from a different enum as an enum value initializer.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	  enum MyOtherEnum {
	   ONE,
	   TWO = MyEnum.TWO,
	   THREE
	 };
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The enum value MyEnum.TWO of type MyEnum may not be used as an initializer for TWO of type MyOtherEnum."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Use a constant with a value of an enum value from a different enum as an enum value initializer.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	 const MyEnum Bar = MyEnum.TWO;

	  enum MyOtherEnum {
	   ONE,
	   TWO = Bar,
	   THREE
	 };
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar with the value MyEnum.TWO may not be used as an initializer for TWO of type MyOtherEnum."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign the default keyword to a variable of enum type.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	 };

	struct Foo{
	  MyEnum x = default;
	};`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The 'default' keyword may not be used with the field x of type MyEnum."})
	}

	////////////////////////////////////////////////////////////
	// Group 4: The left-hand-side is an interface varialbe.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign an enum value to an interface
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface Foo{
	  enum MyEnum {
	    ONE
	  };
	};

	struct Bar{
	  Foo&? x = Foo.MyEnum.ONE;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The enum value Foo.MyEnum.ONE of type Foo.MyEnum may not be assigned to x of type Foo&?."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign constant of value double.INFINITY to an enum variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface Foo{
	  const double BIG = double.INFINITY;
	};

	struct Bar{
	  Foo? x = Foo.BIG;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Foo.BIG with the value double.INFINITY may not be assigned to x of type Foo?."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign double.INFINITY directly to an interface variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface Foo{
	};

	struct Bar{
	  Foo& x = double.INFINITY;
	};
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"double.INFINITY may not be assigned to x of type Foo&."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign the default keyword to a variable of interface type.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 interface Foo{
	};

	struct Bar{
	  Foo& x = default;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"The 'default' keyword may not be used with the field x of type Foo&."})
	}

	////////////////////////////////////////////////////////////
	// Group 5: Multiple indirection
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign doubly-indirected constant of bool type to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const bool Bar = true;
	const bool Baz = Bar;

	struct Foo{
	  int32 x = Baz;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Baz with the value true of type bool may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign doubly-indirected constant with value double.INFINITY  to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const double Bar = double.INFINITY;
	const double Baz = Bar;

	struct Foo{
	  int32 x = Baz;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Baz with the value double.INFINITY may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign doubly-indirected constant of enum value to int32 variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	 enum MyEnum {
	   ONE,
	   TWO,
	   THREE
	 };

	const MyEnum Bar = TWO;
	const MyEnum Baz = Bar;

	struct Foo{
	  int32 x = Baz;
	};`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Baz with the value MyEnum.TWO may not be assigned to x of type int32"})
	}

	////////////////////////////////////////////////////////////
	// Group 4: Invalid EnumValue initializers.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Use string as enum value initializer.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const int32 Foo = 8;
	const string Bar = "fly";

	enum MyEnum {
	   ONE = Foo,
	   TWO = Bar,
	   THREE
	 };`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Bar cannot be used as an enum value initializer because its value, \"fly\", is not a signed 32-bit integer"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Use double.NAN as an enum value initializer.
	////////////////////////////////////////////////////////////
	{
		contents := `
	const int32 Foo = 8;
	const int64 Bar = 9;

	enum MyEnum {
	   ONE = Foo,
	   TWO = Bar,
	   THREE = double.NAN
	 };`

		test.addTestCase(contents, []string{
			"Illegal assignment",
			"double.NAN cannot be used as an enum value initializer"})
	}

	////////////////////////////////////////////////////////////
	// Group 5: Non-referent shadows referent
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Field name shadows constant name
	////////////////////////////////////////////////////////////
	{
		contents := `
	const int32 foo = 42;

    struct MyStruct {
      string foo = "hello";
      int32 bar = foo;
    };`

		test.addTestCase(contents, []string{
			"Error",
			"\"foo\" does not refer to a value. It refers to the field MyStruct.foo at"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Method name shadows constant name
	////////////////////////////////////////////////////////////
	{
		contents := `
	const int32 foo = 42;

    interface MyInterface {
      foo();
      const int32 bar = foo;
    };`

		test.addTestCase(contents, []string{
			"Error",
			"\"foo\" does not refer to a value. It refers to the method MyInterface.foo at"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Field name shadows type name
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct SomethingGood{};

    struct MySecondStruct {
      int32 SomethingGood = 7;
      SomethingGood x = default;
    };`

		test.addTestCase(contents, []string{
			"Error",
			"\"SomethingGood\" does not refer to a type. It refers to the field MySecondStruct.SomethingGood at"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Method name shadows type name
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct SomethingGood{};

    interface MyInterface {
      SomethingGood();

      DoIt(SomethingGood x);
    };`

		test.addTestCase(contents, []string{
			"Error",
			"\"SomethingGood\" does not refer to a type. It refers to the method MyInterface.SomethingGood at"})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := mojom.NewMojomDescriptor()
		specifiedName := ""
		if c.importedFrom == nil {
			specifiedName = c.fileName
		}
		parser := parser.MakeParser(c.fileName, specifiedName, c.mojomContents, descriptor, c.importedFrom)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		err := descriptor.Resolve()
		if err == nil {
			t.Errorf("Resolution unexpectedly succeeded for test case %d.", i)
			continue
		}
		got := err.Error()
		for _, expected := range c.expectedErrors {
			if !strings.Contains(got, expected) {
				t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
			}
		}

	}
}

// TestSingleFileTypeValidationErrors() test the method UserTypeRef.validateAfterResolution().
// It is similar to TestSingleFileValueValidationErrors()
// except that it tests the validation of types phase which occurs before the validation
// of values phase. This phase detects errors that may not be detected during parsing
// but that may be detected without resolving value references.
func TestSingleFileTypeValidationErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Group 1: Left-hand-side is a struct
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Use struct as constant type
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};

    const Foo MyFoo = 3;
	`
		test.addTestCase(contents, []string{
			"The type Foo is not allowed as the type of a declared constant.",
			"Only simple types, strings and enum types may be used"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Use struct as map key
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};

    struct Bar{
    	map<Foo, int32> x;
    };
	`
		test.addTestCase(contents, []string{
			"The type Foo is not allowed as the key type of a map.",
			"Only simple types, strings and enum types may be map keys"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign an integer to a struct variable.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};

    struct Bar{
    	Foo x = 42;
    };
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Field x of type Foo may not be assigned the value 42 of type int8"})
	}

	////////////////////////////////////////////////////////////
	// Group 2: Left-hand-side is an enum
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Assign an integer to an enum variable in a struct field.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum Hats {
		COWBOY,
		TOP
	};

    struct Bar{
    	Hats my_hat = 1;
    };
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Field my_hat of type Hats may not be assigned the value 1 of type int8."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Assign an integer to an enum variable in a constant.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum Hats {
		COWBOY,
		TOP
	};

    const Hats MY_HAT = 1;
	`
		test.addTestCase(contents, []string{
			"Illegal assignment",
			"Const MY_HAT of type Hats may not be assigned the value 1 of type int8."})
	}

	////////////////////////////////////////////////////////////
	// Group 3: Invalid use of interface request.
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Make an interface request out of a struct
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};

    struct Bar{
    	Foo& x;
	};
	`
		test.addTestCase(contents, []string{
			"Invalid interface request specification",
			"Foo&. Foo is not an interface type"})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Make a nullable interface request out of a struct
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	};

    struct Bar{
    	Foo&? x;
	};
	`
		test.addTestCase(contents, []string{
			"Invalid interface request specification",
			"Foo&?. Foo is not an interface type"})
	}

	////////////////////////////////////////////////////////////
	// Group 4: Invalid use of nullable
	////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////////////
	// Test Case: Make nullable enum.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum Hats {
		COWBOY,
		TOP
	};

    struct Bar{
    	Hats? my_hat;
	};
	`
		test.addTestCase(contents, []string{
			"The type Hats? is invalid because Hats is an enum type and these may not be made nullable."})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := mojom.NewMojomDescriptor()
		specifiedName := ""
		if c.importedFrom == nil {
			specifiedName = c.fileName
		}
		parser := parser.MakeParser(c.fileName, specifiedName, c.mojomContents, descriptor, c.importedFrom)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", c.fileName, parser.GetError().Error())
			continue
		}
		err := descriptor.Resolve()
		if err == nil {
			t.Errorf("Resolution unexpectedly succeeded for test case %d.", i)
			continue
		}
		got := err.Error()
		for _, expected := range c.expectedErrors {
			if !strings.Contains(got, expected) {
				t.Errorf("%s:\n*****expected to contain:\n%s\n****actual\n%s", c.fileName, expected, got)
			}
		}

	}
}

////////////////////////////////////////////////
/// Resolution Success Tests
////////////////////////////////////////////////

// testFuncType is a callback used to specify how to test each case.
type testFuncType func(*mojom.MojomDescriptor) error

// singleFileSuccessTestCase stores the data for one test case.
type singleFileSuccessTestCase struct {
	mojomContents string
	testFunc      testFuncType
}

// singleFileSuccessTest contains a series of test cases.
type singleFileSuccessTest struct {
	cases []singleFileSuccessTestCase
}

// addTestCase() should be invoked at the start of a case in
// TestSingleFileResolutionSuccess.
func (test *singleFileSuccessTest) addTestCase(moduleNameSpace, contents string, testFunc testFuncType) {
	test.cases = append(test.cases, singleFileSuccessTestCase{contents, testFunc})
}

// TestSingleFileResolutionSuccess() iterates through a series of test cases.
// For each case we expect for parsing and resolution to succeed. Then we
// execute a given callback test function to test that resolution produced
// the desired result.
func TestSingleFileResolutionSuccess(t *testing.T) {
	test := singleFileSuccessTest{}

	////////////////////////////////////////////////////////////
	// Test Case: A local constant name shadows an enum name.
	////////////////////////////////////////////////////////////
	{
		contents := `
	enum Color{
	  RED, BLUE
	};

	struct MyStruct {
		const Color RED = BLUE;

        Color a_color = RED; // This should resolve to the local constant RED,
                             // and therefore the concrete value should be BLUE.
	};`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*mojom.MojomStruct)
			aColorField := myStructType.FieldsInLexicalOrder[0]
			concreteValue := aColorField.DefaultValue.ResolvedConcreteValue().(*mojom.EnumValue)
			key := concreteValue.ValueKey()
			if key != "TYPE_KEY:Color.BLUE" {
				return fmt.Errorf("%s != TYPE_KEY:Color.BLUE", key)
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: A non-circular list of constant references.
	////////////////////////////////////////////////////////////
	{
		contents := `
		const int32 MyConst1 = MyConst2;
		const int32 MyConst2 = MyConst3;
		const int32 MyConst3 = 42;`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			myConst1 := descriptor.ValuesByKey["TYPE_KEY:MyConst1"].(*mojom.UserDefinedConstant)
			value := myConst1.ValueRef().ResolvedConcreteValue().Value()
			if value != int8(42) {
				return fmt.Errorf("%v(%T) != 42", value, value)
			}

			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Circular reference of enum values
	//
	// NOTE: Even though we have a circular reference of enum values
	// this case passes resolution. But the error will be detected during
	// data computation phase when we attempt to produce integer values
	// for the enum values. See TestEnumComputedDataError in computed_data_test.go. The reason this
	// passes resoultion is that an enum value is a concrete value and so
	// FIRST_VALUE has fully resolved to the concrete value MyEnum.x.
	////////////////////////////////////////////////////////////
	{
		contents := `
		enum MyEnum {
			x = FIRST_VALUE,
			y,
			z
		};
		const MyEnum FIRST_VALUE = MyEnum.x;`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := mojom.NewMojomDescriptor()
		fileName := fmt.Sprintf("file%d", i)
		parser := parser.MakeParser(fileName, fileName, c.mojomContents, descriptor, nil)
		parser.Parse()
		if !parser.OK() {
			t.Errorf("Parsing error for %s: %s", fileName, parser.GetError().Error())
			continue
		}
		err := descriptor.Resolve()
		if err != nil {
			t.Errorf("Resolution failed for test case %d: %s", i, err.Error())
			continue
		}

		if c.testFunc != nil {
			if err := c.testFunc(descriptor); err != nil {
				t.Errorf("%s:\n%s", fileName, err.Error())
				continue
			}
		}
	}
}
