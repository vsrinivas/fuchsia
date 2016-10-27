// Copyright 2016 The Chromium Authors. All rights reserved.
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

// TestIllegalPatterns tests cases where DetectIllFoundedTypes() should return an error.
func TestIllegalPatterns(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      Foo x;
    };`
		test.addTestCase(contents, []string{
			"The type Foo is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: Foo.x -> Foo",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, fixed-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<Foo, 1> x;
    };`
		test.addTestCase(contents, []string{
			"The type Foo is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: Foo.x -> Foo",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, fixed-length array of
	// non-nullable fixed-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<array<Foo, 1>, 1> x;
    };`
		test.addTestCase(contents, []string{
			"The type Foo is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: Foo.x -> Foo",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Union field is non-nullable pointer to union itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    union Foo{
      Foo x;
    };`
		test.addTestCase(contents, []string{
			"The type Foo is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: Foo.x -> Foo",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Union field is non-nullable, fixed-length array of non-nullable pointer to union itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    union Foo{
      array<Foo, 1> x;
    };`
		test.addTestCase(contents, []string{
			"The type Foo is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: Foo.x -> Foo",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: A cycle of length 3.
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct MyStruct1 {
        MyStruct2 x;
    };

    struct MyStruct2 {
        MyStruct3 x;
    };

    struct MyStruct3 {
        MyStruct1 x;
    };`
		test.addTestCase(contents, []string{
			"The type MyStruct1 is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: MyStruct1.x -> MyStruct2.x -> MyStruct3.x -> MyStruct1",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Two structs and a union
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct MyStruct1 {
        MyStruct2 x;
        int32 y;
    };

    struct MyStruct2 {
        MyUnion x;
        int32 y;
    };

    union MyUnion {
        MyStruct1 x;
        MyStruct2 y;
    };`
		test.addTestCase(contents, []string{
			"The type MyStruct1 is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: MyStruct1.x -> MyStruct2.x -> MyUnion.x -> MyStruct1",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Ill-foundedness not found until second pass of algorithm.
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct AEmptyStruct {
    };

    struct BMyUnion {
        AEmptyStruct x;
        CBadStruct   y;
    };

    struct CBadStruct {
       CBadStruct x;
    };`

		test.addTestCase(contents, []string{
			"The type CBadStruct is unserializable: Every instance of this type would include a cycle.",
			"Example cycle: CBadStruct.x -> CBadStruct",
			"One way to break this cycle is to make the field \"x\" nullable.",
		})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse anresolve the mojom input.
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
		if err != nil {
			t.Errorf("Resolution error for %s: %s", c.fileName, err)
			continue
		}
		if err = descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for %s: %s", c.fileName, err)
			continue
		}

		descriptor.SetTestingMode(true)
		err = descriptor.DetectIllFoundedTypes()

		if err == nil {
			t.Errorf("No cycles were detected for test case %d.", i)
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

// TestLegalPatterns tests cases where DetectIllFoundedTypes() should not return an error.
func TestLegalPatterns(t *testing.T) {
	test := singleFileSuccessTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is nullable, pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      Foo? x;
    };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, variable-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    struct Foo{
      array<Foo> x;
    };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is nullable, fixed-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    struct Foo{
      array<Foo, 1>? x;
    };`

		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, fixed-length array of nullable pointer to struct itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    struct Foo{
      array<Foo?, 1> x;
    };`

		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, variable-length array of
	// non-nullable fixed-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<array<Foo, 1>> x;
    };`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, fixed-length array of
	// non-nullable variable-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<array<Foo>, 1> x;
    };`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is non-nullable, fixed-length array of
	// non-nullable fixed-length array of nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<array<Foo?, 1>, 1> x;
    };`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Struct field is nullable, fixed-length array of
	// non-nullable fixed-length array of non-nullable pointer to struct itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      array<array<Foo, 1>, 1>? x;
    };`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Field of struct Foo is a map from a string to a Foo
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct Foo{
      map<string, Foo> x;
    };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field is nullable, pointer to union itself
	////////////////////////////////////////////////////////////
	{
		contents := `
    union Foo{
      Foo? x;
    };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Self-loop is avoidable because of simple type field.
	////////////////////////////////////////////////////////////
	{
		contents := `
    union Foo {
      uint32 field1;
      Foo field2;
     };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Back loop is avoidable because of simple type field.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Bar {
	  Foo x;
	};

    union Foo {
      uint32 field1;
      Bar field2;
     };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field is non-nullable, variable-length array of non-nullable pointer to union itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    union Foo{
      array<Foo> x;
    };`

		testFunc := func(descriptor *mojom.MojomDescriptor) error {
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field is nullable, fixed-length array of non-nullable pointer to union itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    union Foo{
      array<Foo, 1>? x;
    };`

		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: union field is non-nullable, fixed-length array of nullable pointer to union itself
	////////////////////////////////////////////////////////////

	{
		contents := `
    union Foo{
      array<Foo?, 1> x;
    };`

		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: A cycle of length 3 with 1 nullable.
	////////////////////////////////////////////////////////////
	{
		contents := `
    struct MyStruct1 {
        MyStruct2 x;
    };

    struct MyStruct2 {
        MyStruct3? x;
    };

    struct MyStruct3 {
        MyStruct1 x;
    };`
		test.addTestCase("", contents, nil)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Well-founded because of CStruct2
	////////////////////////////////////////////////////////////
	{
		contents := `
    union AUnion0 {
        BStruct1 x;
        CStruct2 y;
    };

    struct BStruct1 {
        int32   x;
        DUnion3 y;
    };

    struct CStruct2 {
       string x;
    };

    union DUnion3 {
    	AUnion0 x;
    	BStruct1 y;
    };`
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

		if err := descriptor.ComputeFinalData(); err != nil {
			t.Errorf("ComputeFinalData error for test case %d: %s", i, err.Error())
			continue
		}

		if err := descriptor.DetectIllFoundedTypes(); err != nil {
			t.Errorf("DetectIllFoundedTypes error for test case %d: %s", i, err.Error())
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
