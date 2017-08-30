// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fidl/compiler/lexer"
	"fmt"
	"testing"
)

func TestUserDefinedTypeSimpleName(t *testing.T) {
	cases := []struct {
		userDefinedType UserDefinedType
		simpleName      string
	}{
		{NewTestStruct("struct"), "struct"},
		{NewTestInterface("interface"), "interface"},
		{NewTestUnion("union"), "union"},
		{NewTestEnum("enum"), "enum"},
	}
	for _, c := range cases {
		got := c.userDefinedType.SimpleName()
		if got != c.simpleName {
			t.Errorf("%v.SimpleName() == %v, want %v", c.userDefinedType, got, c.simpleName)
		}
	}
}

func TestUserDefinedTypeKind(t *testing.T) {
	cases := []struct {
		userDefinedType UserDefinedType
		kind            UserDefinedTypeKind
	}{
		{NewTestStruct("struct"), UserDefinedTypeKindStruct},
		{NewTestInterface("interface"), UserDefinedTypeKindInterface},
		{NewTestUnion("union"), UserDefinedTypeKindUnion},
		{NewTestEnum("enum"), UserDefinedTypeKindEnum},
	}
	for _, c := range cases {
		got := c.userDefinedType.Kind()
		if got != c.kind {
			t.Errorf("%v.Kind() == %v, want %v", c.userDefinedType, got, c.kind)
		}
	}
}

func TestIsAssignmentCompatibleWith(t *testing.T) {
	literalValues := []LiteralValue{
		MakeStringLiteralValue("", nil),
		MakeBoolLiteralValue(false, nil),
		MakeInt64LiteralValue(0, nil),
		MakeDoubleLiteralValue(0, nil),
		MakeDefaultLiteral(nil),
	}
	cases := []struct {
		userDefinedType UserDefinedType
		allowed         []bool
	}{
		{NewTestStruct("struct"), []bool{false, false, false, false, true}},
		{NewTestInterface("interface"), []bool{false, false, false, false, false}},
		{NewTestUnion("union"), []bool{false, false, false, false, false}},
		{NewTestEnum("enum"), []bool{false, false, false, false, false}},
	}
	for _, c := range cases {
		for i, v := range literalValues {
			got := c.userDefinedType.IsAssignmentCompatibleWith(v)
			if got != c.allowed[i] {
				t.Errorf("%v.IsAssignmentCompatibleWith(%d) == %v, want %v", c.userDefinedType, i, got, c.allowed[i])
			}
		}
	}
}

func TestUserDefinedTypeRegisterInScope(t *testing.T) {
	const scopeName = "test.scope"
	scope := NewTestFileScope(scopeName)
	cases := []struct {
		userDefinedType    UserDefinedType
		fullyQualifiedName string
	}{
		{NewTestStruct("struct"), fmt.Sprintf("%s.%s", scopeName, "struct")},
		{NewTestInterface("interface"), fmt.Sprintf("%s.%s", scopeName, "interface")},
		{NewTestUnion("union"), fmt.Sprintf("%s.%s", scopeName, "union")},
		{NewTestEnum("enum"), fmt.Sprintf("%s.%s", scopeName, "enum")},
	}
	for _, c := range cases {
		c.userDefinedType.RegisterInScope(scope)
		got := c.userDefinedType.FullyQualifiedName()
		if got != c.fullyQualifiedName {
			t.Errorf("%v.FullyQualifiedName() == %v, want %v", c.userDefinedType, got, c.fullyQualifiedName)
		}
		if len(c.userDefinedType.TypeKey()) == 0 {
			t.Errorf("%v.TypeKey == ''", c.userDefinedType)
		}
		if c.userDefinedType.Scope() == nil {
			t.Errorf("%v.Scope == nil", c.userDefinedType)
		}
	}
}

func TestStructFieldValidateDefaultValue(t *testing.T) {
	cases := []struct {
		fieldType    TypeRef
		defaultValue ValueRef
		expectOK     bool
	}{
		{SimpleTypeInt64, MakeInt64LiteralValue(42, nil), true},
		{SimpleTypeInt64, MakeStringLiteralValue("42", nil), false},
		{SimpleTypeInt64, MakeDefaultLiteral(nil), false},
		// It is initally OK to assign an unknown user value ref to an integer field.
		{SimpleTypeInt64, NewUserValueRef(AssigneeSpec{
			"assignee",
			SimpleTypeInt64,
		}, "some.type", nil, lexer.Token{}), true},
		{StringType{}, MakeInt64LiteralValue(42, nil), false},
		{StringType{}, MakeStringLiteralValue("42", nil), true},
		{StringType{}, MakeDefaultLiteral(nil), false},
		// It is initally OK to assign an unknown user value ref to an string field.
		{StringType{}, NewUserValueRef(AssigneeSpec{
			"assignee",
			StringType{},
		}, "some.type", nil, lexer.Token{}), true},
	}
	for i, c := range cases {
		structField := NewStructField(
			DeclTestDataWithOrdinal(fmt.Sprintf("field%d", i), int64(i)),
			c.fieldType, c.defaultValue)
		got := structField.ValidateDefaultValue()
		if got != c.expectOK {
			t.Errorf("field %d, type %v, value %v, expected %v", i, c.fieldType, c.defaultValue, c.expectOK)
		}
	}
}

func TestStructFieldMinVersionAttribute(t *testing.T) {
	dummyLiteralValue := MakeBoolLiteralValue(false, &lexer.Token{})
	cases := []struct {
		includeAttributes          bool
		includeMinVersionAttribute bool
		attributeValue             LiteralValue
		expectedValue              uint32
		expectedFound              bool
		expectedOk                 bool
	}{
		{false, false, dummyLiteralValue, 0, false, false},
		{true, false, dummyLiteralValue, 0, false, false},
		{true, true, MakeUint32LiteralValue(123, &lexer.Token{}), 123, true, true},
		{true, true, MakeFloatLiteralValue(1.1, &lexer.Token{}), 0, true, false},
		{true, true, MakeInt64LiteralValue(1234567890123, &lexer.Token{}), 1912276171, true, false},
	}
	for i, c := range cases {
		var attributes *Attributes
		if c.includeAttributes {
			attributes = NewAttributes(lexer.Token{})
			attributes.List = append(attributes.List,
				NewMojomAttribute("dummy", nil, dummyLiteralValue))
			if c.includeMinVersionAttribute {
				attributes.List = append(attributes.List,
					NewMojomAttribute("MinVersion", nil, c.attributeValue))
			}
			attributes.List = append(attributes.List,
				NewMojomAttribute("dummy2", nil, dummyLiteralValue))
		}
		structField := NewStructField(
			DeclTestDataA(fmt.Sprintf("field%d", i), attributes),
			SimpleTypeInt8, nil)
		value, _, found, ok := structField.minVersionAttribute()
		if found != c.expectedFound || ok != c.expectedOk || value != c.expectedValue {
			t.Errorf("case %d: found=%v, ok=%v, value=%v", i, found, ok, value)
		}
	}
}

func TestInterfaceComputeMethodOridnals(t *testing.T) {
	myInterface := NewTestInterface("MyInterface")
	myInterface.InitAsScope(NewTestFileScope("test.scope"))
	cases := []struct {
		assignedOrdinal int64
		expectedOrdinal uint32
	}{
		{-1, 0},
		{1000, 1000},
		{-1, 1001},
		{-1, 1002},
		{2, 2},
		{-1, 3},
		{-1, 4},
	}
	for i, c := range cases {
		myInterface.AddMethod(NewMojomMethod(
			DeclTestDataWithOrdinal(fmt.Sprintf("Method%d", i), c.assignedOrdinal),
			nil, nil))
	}
	if err := myInterface.ComputeMethodOrdinals(); err != nil {
		t.Error(err.Error())
	}
	for i, c := range cases {
		method := myInterface.methodsByName[fmt.Sprintf("Method%d", i)]
		if method == nil {
			t.Error("Missing Method%d", i)
		} else if method.Ordinal != c.expectedOrdinal {
			t.Errorf("Method%d.ordinal=%d, expected %d", i, method.Ordinal, c.expectedOrdinal)
		}
		if method != myInterface.MethodsByOrdinal[method.Ordinal] {
			t.Errorf("methodsByOrdinal[%v] =%v", method.Ordinal, myInterface.MethodsByOrdinal[method.Ordinal])
		}
	}
}

func TestUserDefinedValueSimpleName(t *testing.T) {
	cases := []struct {
		userDefinedValue UserDefinedValue
		simpleName       string
	}{
		{NewTestConstant("const", 42), "const"},
		{NewTestEnumValue("foo"), "foo"},
		{FloatInfinity, "float.INFINITY"},
	}
	for _, c := range cases {
		got := c.userDefinedValue.SimpleName()
		if got != c.simpleName {
			t.Errorf("%v.SimpleName() == %v, want %v", c.userDefinedValue, got, c.simpleName)
		}
	}
}

func TestUserDefinedValueKind(t *testing.T) {
	cases := []struct {
		userDefinedValue UserDefinedValue
		kind             UserDefinedValueKind
	}{
		{NewTestConstant("const", 42), UserDefinedValueKindDeclaredConst},
		{NewTestEnumValue("foo"), UserDefinedValueKindEnumValue},
		{FloatInfinity, UserDefinedValueKindBuiltInConst},
	}
	for _, c := range cases {
		got := c.userDefinedValue.Kind()
		if got != c.kind {
			t.Errorf("%v.Kind() == %v, want %v", c.userDefinedValue, got, c.kind)
		}
	}
}

func TestUserDefinedValueRegisterInScope(t *testing.T) {
	const scopeName = "test.scope"
	scope := NewTestFileScope(scopeName)
	mojomEnum := NewTestEnum("MyEnum")
	mojomEnum.InitAsScope(scope)
	mojomEnum.AddEnumValue(DeclTestData("TheValue"), nil)
	enumValue := mojomEnum.Values[0]
	cases := []struct {
		userDefinedValue   UserDefinedValue
		fullyQualifiedName string
	}{
		{NewTestConstant("const", 42), fmt.Sprintf("%s.%s", scopeName, "const")},
		{enumValue, "test.scope.MyEnum.TheValue"},
		{FloatInfinity, "float.INFINITY"},
	}
	for i, c := range cases {
		value := c.userDefinedValue
		if i == 0 {
			value.RegisterInScope(scope)
		}
		got := value.FullyQualifiedName()
		if got != c.fullyQualifiedName {
			t.Errorf("%v.FullyQualifiedName() == %v, want %v", c.userDefinedValue, got, c.fullyQualifiedName)
		}
		if i != 2 {
			if len(c.userDefinedValue.ValueKey()) == 0 {
				t.Errorf("%v.ValueKey == ''", c.userDefinedValue)
			}
			if c.userDefinedValue.Scope() == nil {
				t.Errorf("%v.Scope == nil", c.userDefinedValue)
			}
		}
	}
}

func TestUserDefinedConstantValidateValue(t *testing.T) {
	cases := []struct {
		declaredType TypeRef
		value        ValueRef
		expectOK     bool
	}{
		{SimpleTypeInt64, MakeInt64LiteralValue(42, nil), true},
		{SimpleTypeInt64, MakeStringLiteralValue("42", nil), false},
		{SimpleTypeInt64, MakeDefaultLiteral(nil), false},
		// It is initally OK to assign an unknown user value ref to an integer field.
		{SimpleTypeInt64, NewUserValueRef(AssigneeSpec{
			"assignee",
			SimpleTypeInt64,
		}, "some.type", nil, lexer.Token{}), true},
		{StringType{}, MakeInt64LiteralValue(42, nil), false},
		{StringType{}, MakeStringLiteralValue("42", nil), true},
		{StringType{}, MakeDefaultLiteral(nil), false},
		// It is initally OK to assign an unknown user value ref to a string field.
		{StringType{}, NewUserValueRef(AssigneeSpec{
			"assignee",
			StringType{},
		}, "some.type", nil, lexer.Token{}), true},
	}
	for i, c := range cases {
		constant := NewUserDefinedConstant(DeclTestData(fmt.Sprintf("constant%d", i)), c.declaredType, c.value)
		got := constant.ValidateValue()
		if got != c.expectOK {
			t.Errorf("constant %d, type %v, value %v, expected %v", i, c.declaredType, c.value, c.expectOK)
		}
	}
}

func TestLookupBuiltInConstantValue(t *testing.T) {
	expectedBuiltInConstantValueNames := []string{
		"float.INFINITY", "float.NEGATIVE_INFINITY", "float.NAN",
		"double.INFINITY", "double.NEGATIVE_INFINITY", "double.NAN"}

	for _, name := range expectedBuiltInConstantValueNames {
		value, ok := LookupBuiltInConstantValue(name)
		if !ok {
			t.Errorf("LookupBuiltInConstantValue(%q) not found.", name)
		} else if value.String() != name {
			t.Errorf("LookupBuiltInConstantValue(%q).String() == %q", name, value.String())
		}
	}
	_, ok := LookupBuiltInConstantValue("this.is.not.a.name")
	if ok {
		t.Errorf("LookupBuiltInConstantValue(%q) did not fail.", "this.is.not.a.name")
	}
}
