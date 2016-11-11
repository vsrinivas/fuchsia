// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fidl/compiler/cmd/fidl/parser"
	"fidl/compiler/core"
	"fmt"
	"strings"
	"testing"
)

// TestStructFieldMinVersionErrors test the method MojomStruct.computeVersionInfo() which
// is invoked by ComputeFinalData. This phase occurs after resolution
// and type validation. We test that different types of errors related to
// the MinVersion attribute are correctly detected.
func TestStructFieldMinVersionErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Float value for MinVersion
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1.1]
	  array<int32>? z;

      [MinVersion = 2]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field z: 1.1. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: string value for MinVersion
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = "1"]
	  array<int32>? z;

      [MinVersion = 2]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field z: \"1\". ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: MinVersion  is negative
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = -1]
	  array<int32>? z;

      [MinVersion = 2]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field z: -1. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: MinVersion  is to big for 32 bits
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1234567890123]
	  array<int32>? z;

      [MinVersion = 2]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field z: 1234567890123. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 2]
	  array<int32>? z;

      [MinVersion = 1]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field w: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This field's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing: ordinals are used.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1]
	  array<int32>? z@3;

      [MinVersion = 2]
	  array<int32>? w@2;
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for field z: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This field's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Non-nullable type used.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct Foo{
	  int32 x;
	  int32 y;

	  [MinVersion = 1]
	  array<int32> z;

      [MinVersion = 2]
	  array<int32>? w;
	};`
		test.addTestCase(contents, []string{
			"Invalid type for field z: array<int32>.",
			"Non-nullable reference fields are only allowed in version 0 of of a struct.",
			"This field's MinVersion is 1."})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse anresolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		err = descriptor.ComputeFinalData()

		if err == nil {
			t.Errorf("Data computation unexpectedly succeeded for test case %d.", i)
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

// TestMethoddMinVersionErrors test the method MojomInterface.computeInterfaceVersion() which
// is invoked by ComputeFinalData. This phase occurs after resolution
// and type validation. We test that different types of errors related to
// the MinVersion attribute are correctly detected.
func TestMethodMinVersionErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Float value for MinVersion on method.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		[MinVersion=1.1]
		DoIt();
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for method DoIt: 1.1. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Float value for MinVersion on request parameter.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt([MinVersion=1.1] int32 x);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for parameter x: 1.1. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Float value for MinVersion on response parameter.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt([MinVersion=1] int32 x) => ([MinVersion=1.1] int32 y);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for response parameter y: 1.1. ",
			"The value must be a non-negative 32-bit integer value."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for mehtods.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		[MinVersion=2]
		DoIt();

		[MinVersion=1]
		DoItAgain();
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for method DoItAgain: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This method's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for request parameters.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt([MinVersion=2] int32 x, [MinVersion=1] int32 y);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for parameter y: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This parameter's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for response parameters.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt() => ([MinVersion=2] int32 x, [MinVersion=1] int32 y);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for response parameter y: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This response parameter's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for methods: ordinals are used.
	////////////////////////////////////////////////////////////
	{

		contents := `
	interface MyInteface {
		DoIt();

		[MinVersion=1]
		AndAgain@2();

		[MinVersion=2]
		DoItAgain@1();
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for method AndAgain: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This method's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for request parameters: ordinals are used
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt([MinVersion=1] int32 y@1, [MinVersion=2] int32 x@0);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for parameter y: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This parameter's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Min Versions must be increasing for response parameters: ordinals are used
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt() => ([MinVersion=1] int32 y@1, [MinVersion=2] int32 x@0);
	};`
		test.addTestCase(contents, []string{
			"Invalid MinVersion attribute for response parameter y: 1. ",
			"The MinVersion must be non-decreasing as a function of the ordinal.",
			" This response parameter's MinVersion must be at least 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Non-nullable type used in request parameters.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt(int32 x, [MinVersion = 1] array<int32>? y, [MinVersion = 2] array<int32> z);
	};`

		test.addTestCase(contents, []string{
			"Invalid type for parameter z: array<int32>.",
			"Non-nullable reference parameters are only allowed in version 0 of of a struct.",
			"This parameter's MinVersion is 2."})
	}

	////////////////////////////////////////////////////////////
	// Test Case: Non-nullable type used in response parameters.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInteface {
		DoIt() => (int32 x, [MinVersion = 1] array<int32>? y, [MinVersion = 2] array<int32> z);
	};`

		test.addTestCase(contents, []string{
			"Invalid type for response parameter z: array<int32>.",
			"Non-nullable reference response parameters are only allowed in version 0 of of a struct.",
			"This response parameter's MinVersion is 2."})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse anresolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		err = descriptor.ComputeFinalData()

		if err == nil {
			t.Errorf("Data computation unexpectedly succeeded for test case %d.", i)
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

func checkStructVersion(description string, structVersion *core.StructVersion,
	expectedVersionNumber, expectedNumFields, expectedNumBytes uint32) error {
	if structVersion.VersionNumber != expectedVersionNumber {
		return fmt.Errorf("%s: VersionNumber %d != %d", description, structVersion.VersionNumber, expectedVersionNumber)
	}
	if structVersion.NumFields != expectedNumFields {
		return fmt.Errorf("%s: for version %d: NumFields= %d != %d", description,
			structVersion.VersionNumber, structVersion.NumFields, expectedNumFields)
	}
	if structVersion.NumBytes != expectedNumBytes {
		return fmt.Errorf("%s: for version %d: NumBytes=%d != %d", description,
			structVersion.VersionNumber, structVersion.NumBytes, expectedNumBytes)
	}
	return nil
}

func checkStructFieldOffsets(description string, mojomStruct *core.MojomStruct,
	expectedOrdinal, expectedOffset []uint32) error {
	if len(mojomStruct.FieldsInOrdinalOrder()) != len(expectedOrdinal) {
		return fmt.Errorf("%s: len(FieldsInOrdinalOrder())=%d != %d", description,
			len(mojomStruct.FieldsInOrdinalOrder()), len(expectedOrdinal))
	}
	for i, field := range mojomStruct.FieldsInLexicalOrder {
		if field != mojomStruct.FieldsInOrdinalOrder()[expectedOrdinal[i]] {
			return fmt.Errorf("%s: i=%d wrong ordinal", description, i)
		}
		if field.Offset() != expectedOffset[i] {
			return fmt.Errorf("%s: i=%d wrong offset %d != %d", description, i,
				field.Offset(), expectedOffset[i])
		}
	}
	return nil
}

// TestStructsComputedData() iterates through a series of test cases.
// For each case we expect for parsing, resolution and final data computation to succeed.
// Then we execute a given callback test function to test that the functions
// computeFieldOffsets and computeVersionData produced the desired result.
func TestStructsComputedData(t *testing.T) {
	test := singleFileSuccessTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Test computeVersionInfo empty struct
	//     Tests that computerVersionInfo never produces an empty
	//     list of versions, even for empty structs.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if len(myStructType.FieldsInOrdinalOrder()) != 0 {
				return fmt.Errorf("len(myStructType.FieldsInOrdinalOrder())=%d", len(myStructType.FieldsInOrdinalOrder()))
			}
			if len(myStructType.VersionInfo()) != 1 {
				return fmt.Errorf("len(myStructType.VersionInfo() = %d", len(myStructType.VersionInfo()))
			}
			if err := checkStructVersion("EmptyStruct", &myStructType.VersionInfo()[0], 0, 0, 8); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test One field
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
		int8 x;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("OneField", myStructType, []uint32{0}, []uint32{0}); err != nil {
				return err
			}
			if err := checkStructVersion("OneField", &myStructType.VersionInfo()[0], 0, 1, 16); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test Padding, In Order
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
		int8  x;
		uint8 y;
		int32 z;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("InOrder", myStructType, []uint32{0, 1, 2}, []uint32{0, 1, 4}); err != nil {
				return err
			}
			if err := checkStructVersion("In order", &myStructType.VersionInfo()[0], 0, 3, 16); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test Padding, Out of order
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
		int8  x;
		int32 y;
		uint8 z;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("OutOfOrder", myStructType, []uint32{0, 1, 2}, []uint32{0, 4, 1}); err != nil {
				return err
			}
			if err := checkStructVersion("OutOfOrder", &myStructType.VersionInfo()[0], 0, 3, 16); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test Padding, Overflow
	// 2 bytes should be packed together first, followed by short, then by int.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
		int8   f1;
		int32  f2;
		int16  f3;
		int8   f4;
		int8   f5;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("Overflow", myStructType, []uint32{0, 1, 2, 3, 4}, []uint32{0, 4, 2, 1, 8}); err != nil {
				return err
			}
			if err := checkStructVersion("Overflow", &myStructType.VersionInfo()[0], 0, 5, 24); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Nullable Types
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface{};

	struct MyStruct {
		string?    f1;
		handle?    f2;
		MyStruct?  f3;
		handle<data_pipe_consumer>?   f4;
		array<int8>?  f5;
		handle<data_pipe_producer>?   f6;
		array<int8, 5>?  f7;
		handle<channel>?   f8;
		MyInterface? f9;
		handle<vmo>?   f10;
		MyInterface&? f11;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("Nullable Types", myStructType,
				[]uint32{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
				[]uint32{0, 8, 16, 12, 24, 32, 40, 36, 48, 56, 60}); err != nil {
				return err
			}
			if err := checkStructVersion("Nullable Types", &myStructType.VersionInfo()[0], 0, 11, 72); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: All Types
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyStruct2{};

	struct MyStruct {
		bool    f1;
		int8   f2;
		string  f3;
		uint8  f4;
		int16  f5;
		double   f6;
		uint16 f7;
		int32   f8;
		uint32 f9;
		int64   f10;
		float f11;
		string f12;
		handle f13;
		uint64 f14;
		MyStruct2 f15;
		array<int32> f16;
		string? f17;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("All Types", myStructType,
				[]uint32{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
				[]uint32{0, 1, 8, 2, 4, 16, 6, 24, 28, 32, 40, 48, 44, 56, 64, 72, 80}); err != nil {
				return err
			}
			if err := checkStructVersion("All Types", &myStructType.VersionInfo()[0], 0, 17, 96); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Bools
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyStruct2{};

	struct MyStruct {
		bool    f1;
		bool   f2;
		int32  f3;
		bool  f4;
		bool  f5;
		bool  f6;
		bool f7;
		bool   f8;
		bool f9;
		bool  f10;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("Bools", myStructType,
				[]uint32{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
				[]uint32{0, 0, 4, 0, 0, 0, 0, 0, 0, 1}); err != nil {
				return err
			}
			if err := checkStructVersion("Bools", &myStructType.VersionInfo()[0], 0, 10, 16); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test computeVersionInfo complex order
	//     Tests computerVersionInfo using a struct wose definition order,
	//     ordinal order and pack order for fields are all different.
	////////////////////////////////////////////////////////////
	{
		contents := `
	struct MyStruct {
		[MinVersion = 3]
		bool   field3@3;

		int32  field_0@0;

		[MinVersion = 2]
		int64  field_1@1;

        [MinVersion = 3]
		int64  field_2@2;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("ComplexOrder", myStructType, []uint32{3, 0, 1, 2}, []uint32{4, 0, 8, 16}); err != nil {
				return err
			}
			if err := checkStructVersion("ComplexOrder", &myStructType.VersionInfo()[0], 0, 1, 16); err != nil {
				return err
			}
			if err := checkStructVersion("ComplexOrder", &myStructType.VersionInfo()[1], 2, 2, 24); err != nil {
				return err
			}
			if err := checkStructVersion("ComplexOrder", &myStructType.VersionInfo()[2], 3, 4, 32); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Test Interface Alignment
	//     Tests that interfaces are aligned on 4-byte boundaries,
	//     although the size of an interface is 8 bytes.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface{};

	struct MyStruct {
		int32       x;
		MyInterface y;
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myStructType := descriptor.TypesByKey["TYPE_KEY:MyStruct"].(*core.MojomStruct)
			if err := checkStructFieldOffsets("InterfaceAlignment", myStructType, []uint32{0, 1}, []uint32{0, 4}); err != nil {
				return err
			}
			if err := checkStructVersion("InterfaceAlignment",
				&myStructType.VersionInfo()[0], 0, 2, 24); err != nil {
				return err
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		if c.testFunc != nil {
			if err := c.testFunc(descriptor); err != nil {
				t.Errorf("%s:\n%s", fileName, err.Error())
				continue
			}
		}
	}
}

// TestInterfaceComputedData() iterates through a series of test cases.
// For each case we expect for parsing, resolution and final data computation to succeed.
// Then we execute a given callback test function to test that the methods
// MojomInterface.ComputeFinalData() and MojomInterface.computeInterfaceVersion()
//  produced the desired result.
func TestInterfaceComputedData(t *testing.T) {
	test := singleFileSuccessTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Test that an empty interface still gets a version.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface {
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myInterfaceType := descriptor.TypesByKey["TYPE_KEY:MyInterface"].(*core.MojomInterface)
			if myInterfaceType.Version() != 0 {
				return fmt.Errorf("myInterfaceType.Version()=%d", myInterfaceType.Version())
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Interface version comes from MinVersion on a method
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface {
		Method1();
		Method2();
		[MinVersion=7]
		Method3();
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myInterfaceType := descriptor.TypesByKey["TYPE_KEY:MyInterface"].(*core.MojomInterface)
			if myInterfaceType.Version() != 7 {
				return fmt.Errorf("myInterfaceType.Version()=%d", myInterfaceType.Version())
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Interface version comes from MinVersion on a parameter
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface {
		Method1([MinVersion=1] int8 x, [MinVersion=8] int8 y);
		Method2();
		[MinVersion=7]
		Method3();
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myInterfaceType := descriptor.TypesByKey["TYPE_KEY:MyInterface"].(*core.MojomInterface)
			if myInterfaceType.Version() != 8 {
				return fmt.Errorf("myInterfaceType.Version()=%d", myInterfaceType.Version())
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Test Case: Interface version comes from MinVersion on a response parameter.
	////////////////////////////////////////////////////////////
	{
		contents := `
	interface MyInterface {
		Method1([MinVersion=1] int8 x, [MinVersion=8] int8 y);
		Method2() => ([MinVersion=1] int8 x, [MinVersion=9] int8 y);
		[MinVersion=7]
		Method3();
	};`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			myInterfaceType := descriptor.TypesByKey["TYPE_KEY:MyInterface"].(*core.MojomInterface)
			if myInterfaceType.Version() != 9 {
				return fmt.Errorf("myInterfaceType.Version()=%d", myInterfaceType.Version())
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		if c.testFunc != nil {
			if err := c.testFunc(descriptor); err != nil {
				t.Errorf("%s:\n%s", fileName, err.Error())
				continue
			}
		}
	}
}

// TestEnumComputedDataErrors test the method MojomEnum.ComputeEnumValueIntegers which
// is invoked by ComputeFinalData. This phase occurs after resolution
// and type validation. We test that different types of errors are correctly detected.
func TestEnumComputedDataErrors(t *testing.T) {
	test := singleFileTest{}

	////////////////////////////////////////////////////////////
	// Test Case: Test that a circular reference of enum value definitions is detected.
	////////////////////////////////////////////////////////////
	{
		contents := `
		enum MyEnum {
			x = FIRST_VALUE,
			y,
			z
		};
		const MyEnum FIRST_VALUE = MyEnum.x;`
		test.addTestCase(contents, []string{
			"The reference FIRST_VALUE is being used as an enum value initializer",
			"but it has resolved to a different enum value that itself does not yet have an integer value."})
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse anresolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		err = descriptor.ComputeFinalData()

		if err == nil {
			t.Errorf("Data computation unexpectedly succeeded for test case %d.", i)
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

// TestEnumComputedData() iterates through a series of test cases.
// For each case we expect for parsing, resolution and final data computation to succeed.
// Then we execute a given callback test function to test that the methods
// MojomEnum.ComputeFinalData() produced the desired result.
func TestEnumComputedData(t *testing.T) {
	test := singleFileSuccessTest{}

	////////////////////////////////////////////////////////////
	// Test Case: A non-circular chain of enum value definitions.
	////////////////////////////////////////////////////////////
	{
		contents := `
		enum MyEnum {
			x,
			y  = FIRST_VALUE,
			z
		};
		const MyEnum FIRST_VALUE = MyEnum.x;`

		testFunc := func(descriptor *core.MojomDescriptor) error {
			xValue := descriptor.ValuesByKey["TYPE_KEY:MyEnum.x"].(*core.EnumValue)
			yValue := descriptor.ValuesByKey["TYPE_KEY:MyEnum.y"].(*core.EnumValue)
			zValue := descriptor.ValuesByKey["TYPE_KEY:MyEnum.z"].(*core.EnumValue)
			if xValue.ComputedIntValue != 0 {
				return fmt.Errorf("xValue.ComputedIntValue=%d", xValue.ComputedIntValue)
			}
			if yValue.ComputedIntValue != 0 {
				return fmt.Errorf("yValue.ComputedIntValue=%d", yValue.ComputedIntValue)
			}
			if zValue.ComputedIntValue != 1 {
				return fmt.Errorf("zValue.ComputedIntValue=%d", zValue.ComputedIntValue)
			}
			return nil
		}
		test.addTestCase("", contents, testFunc)
	}

	////////////////////////////////////////////////////////////
	// Execute all of the test cases.
	////////////////////////////////////////////////////////////
	for i, c := range test.cases {
		// Parse and resolve the mojom input.
		descriptor := core.NewMojomDescriptor()
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

		if c.testFunc != nil {
			if err := c.testFunc(descriptor); err != nil {
				t.Errorf("%s:\n%s", fileName, err.Error())
				continue
			}
		}
	}
}
