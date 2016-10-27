// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"math"
	"mojom/mojom_tool/lexer"
	"testing"
)

func TestConcreteTypeKind(t *testing.T) {
	cases := []struct {
		concreteType ConcreteType
		kind         TypeKind
	}{
		{SimpleTypeBool, TypeKindSimple},
		{SimpleTypeDouble, TypeKindSimple},
		{SimpleTypeFloat, TypeKindSimple},
		{SimpleTypeInt8, TypeKindSimple},
		{SimpleTypeInt16, TypeKindSimple},
		{SimpleTypeInt32, TypeKindSimple},
		{SimpleTypeInt64, TypeKindSimple},
		{SimpleTypeUInt8, TypeKindSimple},
		{SimpleTypeUInt16, TypeKindSimple},
		{SimpleTypeUInt32, TypeKindSimple},
		{SimpleTypeUInt64, TypeKindSimple},
		{StringLiteralType, TypeKindString},
		{NewMojomEnum(DeclTestData("")), TypeKindUserDefined},
		{BuiltInConstant, TypeKindUserDefined},
	}
	for _, c := range cases {
		got := c.concreteType.ConcreteTypeKind()
		if got != c.kind {
			t.Errorf("%v.ConcreteTypeKind() == %v, want %v", c.concreteType, got, c.kind)
		}
	}
}

func TestTypeRefKind(t *testing.T) {
	cases := []struct {
		typeRef TypeRef
		kind    TypeKind
	}{
		{SimpleTypeBool, TypeKindSimple},
		{SimpleTypeDouble, TypeKindSimple},
		{SimpleTypeFloat, TypeKindSimple},
		{SimpleTypeInt8, TypeKindSimple},
		{SimpleTypeInt16, TypeKindSimple},
		{SimpleTypeInt32, TypeKindSimple},
		{SimpleTypeInt64, TypeKindSimple},
		{SimpleTypeUInt8, TypeKindSimple},
		{SimpleTypeUInt16, TypeKindSimple},
		{SimpleTypeUInt32, TypeKindSimple},
		{SimpleTypeUInt64, TypeKindSimple},
		{StringType{}, TypeKindString},
		{NewArrayTypeRef(SimpleTypeInt32, 0, false), TypeKindArray},
		{NewMapTypeRef(SimpleTypeInt32, SimpleTypeInt64, false), TypeKindMap},
		{BuiltInType("handle"), TypeKindHandle},
		{NewUserTypeRef("foo", false, false, nil, lexer.Token{}), TypeKindUserDefined},
	}
	for _, c := range cases {
		got := c.typeRef.TypeRefKind()
		if got != c.kind {
			t.Errorf("%v.TypeRefKind() == %v, want %v", c.typeRef, got, c.kind)
		}
	}
}

func TestMarkUsedAsMapKey(t *testing.T) {
	userTypeRef := NewUserTypeRef("foo", false, false, nil, lexer.Token{})
	cases := []struct {
		typeRef TypeRef
		allowed bool
	}{
		{SimpleTypeBool, true},
		{SimpleTypeDouble, true},
		{SimpleTypeFloat, true},
		{SimpleTypeInt8, true},
		{SimpleTypeInt16, true},
		{SimpleTypeInt32, true},
		{SimpleTypeInt64, true},
		{SimpleTypeUInt8, true},
		{SimpleTypeUInt16, true},
		{SimpleTypeUInt32, true},
		{SimpleTypeUInt64, true},
		{StringType{}, true},
		{NewArrayTypeRef(SimpleTypeInt32, 0, false), false},
		{NewMapTypeRef(SimpleTypeInt32, SimpleTypeInt64, false), false},
		{BuiltInType("handle"), false},
		{userTypeRef, true},
	}
	for _, c := range cases {
		got := c.typeRef.MarkUsedAsMapKey()
		if got != c.allowed {
			t.Errorf("%v.MarkUsedAsMapKey() == %v, want %v", c.typeRef, got, c.allowed)
		}
	}
	if !userTypeRef.usedAsMapKey {
		t.Error("userTypeRef.usedAsMapKey is false.")
	}
}

func TestMarkUsedAsConstantType(t *testing.T) {
	userTypeRef := NewUserTypeRef("foo", false, false, nil, lexer.Token{})
	cases := []struct {
		typeRef TypeRef
		allowed bool
	}{
		{SimpleTypeBool, true},
		{SimpleTypeDouble, true},
		{SimpleTypeFloat, true},
		{SimpleTypeInt8, true},
		{SimpleTypeInt16, true},
		{SimpleTypeInt32, true},
		{SimpleTypeInt64, true},
		{SimpleTypeUInt8, true},
		{SimpleTypeUInt16, true},
		{SimpleTypeUInt32, true},
		{SimpleTypeUInt64, true},
		{StringType{}, true},
		{NewArrayTypeRef(SimpleTypeInt32, 0, false), false},
		{NewMapTypeRef(SimpleTypeInt32, SimpleTypeInt64, false), false},
		{BuiltInType("handle"), false},
		{userTypeRef, true},
	}
	for _, c := range cases {
		got := c.typeRef.MarkUsedAsConstantType()
		if got != c.allowed {
			t.Errorf("%v.MarkUsedAsConstantType() == %v, want %v", c.typeRef, got, c.allowed)
		}
	}
	if !userTypeRef.usedAsConstantType {
		t.Error("userTypeRef.usedAsConstantType is false.")
	}
}

func TestMarkTypeCompatible(t *testing.T) {
	userTypeRef := NewUserTypeRef("foo", false, false, nil, lexer.Token{})
	literalValues := []LiteralValue{
		MakeStringLiteralValue("", nil),
		MakeBoolLiteralValue(false, nil),
		MakeInt8LiteralValue(0, nil),
		MakeInt8LiteralValue(-1, nil),
		MakeInt32LiteralValue(1, nil),
		MakeUint32LiteralValue(1, nil),
		MakeInt64LiteralValue(-(1 << 24), nil),
		MakeInt64LiteralValue(1<<25, nil),
		MakeInt64LiteralValue(1<<53, nil),
		MakeInt64LiteralValue(-(1 << 54), nil),
		MakeFloatLiteralValue(math.MaxFloat32, nil),
		MakeDoubleLiteralValue(math.MaxFloat32*2, nil),
		MakeDefaultLiteral(nil),
	}
	cases := []struct {
		typeRef TypeRef
		allowed []int
	}{
		// In the data below 0 indicates not allowed and 1 indicates allowed.
		{SimpleTypeBool, []int{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeDouble, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0}},
		{SimpleTypeFloat, []int{0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0}},
		{SimpleTypeInt8, []int{0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeInt16, []int{0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeInt32, []int{0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeInt64, []int{0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0}},
		{SimpleTypeUInt8, []int{0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeUInt16, []int{0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeUInt32, []int{0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0}},
		{SimpleTypeUInt64, []int{0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0}},
		{StringType{}, []int{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{NewArrayTypeRef(SimpleTypeInt32, 0, false), []int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{NewMapTypeRef(SimpleTypeInt32, SimpleTypeInt64, false), []int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		{BuiltInType("handle"), []int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
		// Assignments to UserTypeRefs are not validated at all during parsing.
		{userTypeRef, []int{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
	}
	for _, c := range cases {
		for i, v := range literalValues {
			got := c.typeRef.MarkTypeCompatible(LiteralAssignment{assignedValue: v})
			if got != (c.allowed[i] == 1) {
				t.Errorf("%v.MarkTypeCompatible(%d) == %v, want %v", c.typeRef, i, got, c.allowed[i])
			}
		}
	}
	if !userTypeRef.literalAssignment.assignedValue.IsDefault() {
		t.Error("userTypeRef.literalAssignment.assignedValue.IsDefault() is false.")
	}
}

func TestBuiltInType(t *testing.T) {
	expectedBuiltInTypeNames := []string{
		"bool", "double", "float", "int8", "int16", "int32", "int64",
		"uint8", "uint16", "uint32", "uint64", "string", "string?",
		"handle", "handle<message_pipe>", "handle<data_pipe_producer>",
		"handle<data_pipe_consumer>", "handle<shared_buffer>",
		"handle?", "handle<message_pipe>?", "handle<data_pipe_producer>?",
		"handle<data_pipe_consumer>?", "handle<shared_buffer>?"}

	for _, name := range expectedBuiltInTypeNames {
		if BuiltInType(name) == nil {
			t.Errorf("BuiltInType(%q) not found.", name)
		} else if BuiltInType(name).String() != name {
			t.Errorf("BuiltInType(%q).String() != %q", name, name)
		}
	}
}

func TestMarkUsedAsEnumValueInitializer(t *testing.T) {
	userTypeRef := NewUserTypeRef("foo", false, false, nil, lexer.Token{})
	assigneeSpec := AssigneeSpec{
		"assignee",
		userTypeRef,
	}
	userValueRef := NewUserValueRef(assigneeSpec, "foo", nil, lexer.Token{})
	cases := []struct {
		valueRef ValueRef
		allowed  bool
	}{
		{MakeStringLiteralValue("", nil), false},
		{MakeBoolLiteralValue(false, nil), false},
		{MakeUint8LiteralValue(0, nil), true},
		{MakeInt8LiteralValue(-1, nil), true},
		{MakeInt64LiteralValue(math.MaxInt32, nil), true},
		{MakeInt64LiteralValue(math.MaxInt32+1, nil), false},
		{MakeInt64LiteralValue(math.MinInt32, nil), true},
		{MakeInt64LiteralValue(math.MinInt32-1, nil), false},
		{MakeDoubleLiteralValue(3.14, nil), false},
		{MakeDefaultLiteral(nil), false},
		{userValueRef, true},
	}
	for _, c := range cases {
		got := c.valueRef.MarkUsedAsEnumValueInitializer()
		if got != c.allowed {
			t.Errorf("%v.MarkUsedAsEnumValueInitializer() == %v, want %v", c.valueRef, got, c.allowed)
		}
	}
	if !userValueRef.usedAsEnumValueInitializer {
		t.Error("userValueRef.usedAsEnumValueInitializer is false.")
	}
}

func TestResolvedConcreteValue(t *testing.T) {
	userTypeRef := NewUserTypeRef("foo", false, false, nil, lexer.Token{})
	assigneeSpec := AssigneeSpec{
		"assignee",
		userTypeRef,
	}
	userValueRef := NewUserValueRef(assigneeSpec, "foo", nil, lexer.Token{})
	cases := []struct {
		valueRef      ValueRef
		concreteValue ConcreteValue
	}{
		{MakeStringLiteralValue("foo", nil), MakeStringLiteralValue("foo", nil)},
		{MakeBoolLiteralValue(false, nil), MakeBoolLiteralValue(false, nil)},
		{MakeInt64LiteralValue(42, nil), MakeInt64LiteralValue(42, nil)},
		{MakeDoubleLiteralValue(3.14, nil), MakeDoubleLiteralValue(3.14, nil)},
		{MakeDefaultLiteral(nil), MakeDefaultLiteral(nil)},
		{userValueRef, nil},
	}
	for _, c := range cases {
		got := c.valueRef.ResolvedConcreteValue()
		if got != c.concreteValue {
			t.Errorf("%v.ResolvedConcreteValue() == %v, want %v", c.valueRef, got, c.concreteValue)
		}
	}
}

func TestValueType(t *testing.T) {
	mojomEnum := NewTestEnum("foo")
	mojomEnum.AddEnumValue(DeclTestData("bar"), nil)
	cases := []struct {
		concreteValue ConcreteValue
		concreteType  ConcreteType
	}{
		{MakeStringLiteralValue("foo", nil), StringLiteralType},
		{MakeBoolLiteralValue(false, nil), SimpleTypeBool},
		{MakeInt64LiteralValue(42, nil), SimpleTypeInt64},
		{MakeDoubleLiteralValue(3.14, nil), SimpleTypeDouble},
		{MakeDefaultLiteral(nil), StringLiteralType},
		{mojomEnum.Values[0], mojomEnum},
		{FloatInfinity, BuiltInConstant},
	}
	for _, c := range cases {
		got := c.concreteValue.ValueType()
		if got != c.concreteType {
			t.Errorf("%v.ValueType() == %v, want %v", c.concreteValue, got, c.concreteType)
		}
	}
}

func TestValue(t *testing.T) {
	enumValue := NewTestEnumValue("foo")
	cases := []struct {
		concreteValue ConcreteValue
		value         interface{}
	}{
		{MakeStringLiteralValue("foo", nil), "foo"},
		{MakeBoolLiteralValue(false, nil), false},
		{MakeInt64LiteralValue(42, nil), int64(42)},
		{MakeDoubleLiteralValue(3.14, nil), 3.14},
		{MakeDefaultLiteral(nil), "default"},
		{enumValue, *enumValue},
		{FloatInfinity, FloatInfinity},
	}
	for _, c := range cases {
		got := c.concreteValue.Value()
		if got != c.value {
			t.Errorf("%v.Value() == %v, want %v", c.concreteValue, got, c.value)
		}
	}
}

func TestValidateAfterResolution(t *testing.T) {
	stringLiteral := MakeStringLiteralValue("foo", nil)
	intLiteral := MakeInt64LiteralValue(42, nil)
	floatLiteral := MakeDoubleLiteralValue(3.14, nil)
	defaultLiteral := MakeDefaultLiteral(nil)
	cases := []struct {
		typeRef       *UserTypeRef
		expectSuccess bool
	}{
		// The bool arguments to NewResolvedStructRef() and
		// NewResolvedEnumRef are: (usedAsMapKey, usedForConstant)
		{NewResolvedStructRef(false, false, nil), true},
		// A struct may not be the type of a constant.
		{NewResolvedStructRef(false, true, nil), false},
		// A struct may not be used as a map key.
		{NewResolvedStructRef(true, false, nil), false},
		// A struct variable may not be assigned a literal other than "default".
		{NewResolvedStructRef(false, false, &stringLiteral), false},
		{NewResolvedStructRef(false, false, &intLiteral), false},
		{NewResolvedStructRef(false, false, &floatLiteral), false},
		{NewResolvedStructRef(false, false, &defaultLiteral), true},

		{NewResolvedEnumRef(false, false, nil), true},
		// An enum type may be the type of a constant
		{NewResolvedEnumRef(false, true, nil), true},
		// An enum type may be the type of a map key
		{NewResolvedEnumRef(true, false, nil), true},
		{NewResolvedEnumRef(false, false, &stringLiteral), false},
		// Enums may not be assigned an integer literal
		{NewResolvedEnumRef(false, false, &intLiteral), false},
		{NewResolvedEnumRef(false, false, &floatLiteral), false},
		{NewResolvedEnumRef(false, false, &defaultLiteral), false},
	}
	for i, c := range cases {
		success := nil == c.typeRef.validateAfterResolution()
		if success != c.expectSuccess {
			t.Errorf("case %d: %v: success=%v", i, c.typeRef, success)
		}
	}
}

func TestUint32Value(t *testing.T) {
	cases := []struct {
		literalValue    LiteralValue
		expectedValue   uint32
		expectedSuccess bool
	}{
		{MakeUint32LiteralValue(12345, nil), 12345, true},
		{MakeInt64LiteralValue(12345, nil), 12345, true},
		{MakeInt64LiteralValue(1234567890123456, nil), 1015724736, false},
		{MakeInt32LiteralValue(-12345, nil), 4294954951, false},
		{MakeFloatLiteralValue(1234.5, nil), 0, false},
		{MakeStringLiteralValue("12345", nil), 0, false},
		{MakeBoolLiteralValue(false, nil), 0, false},
	}
	for _, c := range cases {
		got, ok := uint32Value(c.literalValue)
		if ok != c.expectedSuccess || got != c.expectedValue {
			t.Errorf("uint32Value(%v) == %v, %v, want %v, %v", c.literalValue,
				got, ok, c.expectedValue, c.expectedSuccess)
		}
	}
}
