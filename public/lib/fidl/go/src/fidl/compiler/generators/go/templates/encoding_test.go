// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"testing"

	"mojom/generators/go/translator"
)

type mockEncodingInfo struct {
	translator.EncodingInfo
	isSimple            bool
	isPointer           bool
	isHandle            bool
	isArray             bool
	isMap               bool
	isNullable          bool
	isStruct            bool
	isUnion             bool
	isEnum              bool
	isInterface         bool
	isInterfaceRequest  bool
	elementEncodingInfo *mockEncodingInfo
	keyEncodingInfo     *mockEncodingInfo
	valueEncodingInfo   *mockEncodingInfo
	bitSize             uint32
	writeFunction       string
	readFunction        string
	identifier          string
	goType              string
}

func (m mockEncodingInfo) IsSimple() bool                               { return m.isSimple }
func (m mockEncodingInfo) IsPointer() bool                              { return m.isPointer }
func (m mockEncodingInfo) IsHandle() bool                               { return m.isHandle }
func (m mockEncodingInfo) IsArray() bool                                { return m.isArray }
func (m mockEncodingInfo) IsMap() bool                                  { return m.isMap }
func (m mockEncodingInfo) IsNullable() bool                             { return m.isNullable }
func (m mockEncodingInfo) IsStruct() bool                               { return m.isStruct }
func (m mockEncodingInfo) IsUnion() bool                                { return m.isUnion }
func (m mockEncodingInfo) IsEnum() bool                                 { return m.isEnum }
func (m mockEncodingInfo) IsInterface() bool                            { return m.isInterface }
func (m mockEncodingInfo) IsInterfaceRequest() bool                     { return m.isInterfaceRequest }
func (m mockEncodingInfo) ElementEncodingInfo() translator.EncodingInfo { return m.elementEncodingInfo }
func (m mockEncodingInfo) KeyEncodingInfo() translator.EncodingInfo     { return m.keyEncodingInfo }
func (m mockEncodingInfo) ValueEncodingInfo() translator.EncodingInfo   { return m.valueEncodingInfo }
func (m mockEncodingInfo) BitSize() uint32                              { return m.bitSize }
func (m mockEncodingInfo) WriteFunction() string                        { return m.writeFunction }
func (m mockEncodingInfo) ReadFunction() string                         { return m.readFunction }
func (m mockEncodingInfo) Identifier() string                           { return m.identifier }
func (m mockEncodingInfo) GoType() string                               { return m.goType }
func (m mockEncodingInfo) UnionDecodeFunction() string                  { return "Decode" + m.goType }

func (m mockEncodingInfo) BaseGoType() string {
	goType := m.GoType()
	if goType[0] == '*' {
		return goType[1:]
	}
	return goType
}

func (m mockEncodingInfo) DerefIdentifier() (id string) {
	id = m.Identifier()
	if m.IsNullable() && !m.IsUnion() {
		id = "*" + id
	}
	return
}

func TestEncodingSimpleFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WriteUint8(s.Fuint8); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isSimple:      true,
		identifier:    "s.Fuint8",
		writeFunction: "WriteUint8",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingHandleFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WriteHandle(s.SomeHandle); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isHandle:   true,
		identifier: "s.SomeHandle",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableHandleFieldEncoding(t *testing.T) {
	expected := `if s.SomeHandle == nil {
	encoder.WriteInvalidHandle()
} else {
	if err := encoder.WriteHandle(*s.SomeHandle); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isHandle:   true,
		identifier: "s.SomeHandle",
		isNullable: true,
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingStringFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
	return err
}
if err := encoder.WriteString(s.FString); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isSimple:      true,
		identifier:    "s.FString",
		writeFunction: "WriteString",
		isPointer:     true,
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableStringFieldEncoding(t *testing.T) {
	expected := `if s.FString == nil {
	encoder.WriteNullPointer()
} else {
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(*s.FString); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isSimple:      true,
		identifier:    "s.FString",
		writeFunction: "WriteString",
		isPointer:     true,
		isNullable:    true,
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingArrayOfArrayOfUint16(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
	return err
}
encoder.StartArray(uint32(len(s.ArrayOfArrayOfInt)), 64)
for _, elem := range s.ArrayOfArrayOfInt {
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(elem)), 16)
	for _, elem1 := range elem {
		if err := encoder.WriteUint16(elem1); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
}
if err := encoder.Finish(); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isPointer:  true,
		isArray:    true,
		identifier: "s.ArrayOfArrayOfInt",
		elementEncodingInfo: &mockEncodingInfo{
			isPointer:  true,
			isArray:    true,
			bitSize:    64,
			identifier: "elem",
			elementEncodingInfo: &mockEncodingInfo{
				isSimple:      true,
				bitSize:       16,
				writeFunction: "WriteUint16",
				identifier:    "elem1",
			},
		},
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingMapUint16ToInt32(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
	return err
}
encoder.StartMap()
{
	var keys []uint16
	var values []int32
	for key := range s.MapUint16ToInt32 {
		keys = append(keys, key)
	}
	if encoder.Deterministic() {
		bindings.SortMapKeys(&keys)
	}
	for _, key := range keys {
		values = append(values, s.MapUint16ToInt32[key])
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(keys)), 16)
	for _, key := range keys {
		if err := encoder.WriteUint16(key); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(values)), 32)
	for _, value := range values {
		if err := encoder.WriteInt32(value); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
}
if err := encoder.Finish(); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isPointer:  true,
		isMap:      true,
		identifier: "s.MapUint16ToInt32",
		keyEncodingInfo: &mockEncodingInfo{
			isPointer:  true,
			isArray:    true,
			bitSize:    64,
			identifier: "keys",
			goType:     "[]uint16",
			elementEncodingInfo: &mockEncodingInfo{
				isSimple:      true,
				bitSize:       16,
				writeFunction: "WriteUint16",
				identifier:    "key",
			},
		},
		valueEncodingInfo: &mockEncodingInfo{
			isPointer:  true,
			isArray:    true,
			bitSize:    64,
			identifier: "values",
			goType:     "[]int32",
			elementEncodingInfo: &mockEncodingInfo{
				isSimple:      true,
				bitSize:       32,
				writeFunction: "WriteInt32",
				identifier:    "value",
			},
		},
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingStructFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
	return err
}
if err := s.FStruct.Encode(encoder); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isPointer:  true,
		isStruct:   true,
		identifier: "s.FStruct",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableStructFieldEncoding(t *testing.T) {
	expected := `if s.FNullableStruct == nil {
	encoder.WriteNullPointer()
} else {
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := s.FNullableStruct.Encode(encoder); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isPointer:  true,
		isStruct:   true,
		isNullable: true,
		identifier: "s.FNullableStruct",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingUnionFieldEncoding(t *testing.T) {
	expected := `if s.FUnion == nil {
	return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
} else {
	if err := s.FUnion.Encode(encoder); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isUnion:    true,
		identifier: "s.FUnion",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableUnionFieldEncoding(t *testing.T) {
	expected := `if s.FUnion == nil {
	encoder.WriteNullUnion()
} else {
	if err := s.FUnion.Encode(encoder); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isNullable: true,
		isUnion:    true,
		identifier: "s.FUnion",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNestedUnionFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
		return err
}
encoder.StartNestedUnion()
if s.FUnion == nil {
	return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
} else {
	if err := s.FUnion.Encode(encoder); err != nil {
		return err
	}
}
encoder.Finish()`

	encodingInfo := mockEncodingInfo{
		isPointer:  true,
		isUnion:    true,
		identifier: "s.FUnion",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNestedNullableUnionFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WritePointer(); err != nil {
		return err
}
encoder.StartNestedUnion()
if s.FUnion == nil {
	encoder.WriteNullUnion()
} else {
	if err := s.FUnion.Encode(encoder); err != nil {
		return err
	}
}
encoder.Finish()`

	encodingInfo := mockEncodingInfo{
		isNullable: true,
		isPointer:  true,
		isUnion:    true,
		identifier: "s.FUnion",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingEnumFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WriteInt32(int32(s.EnumField)); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isEnum:     true,
		identifier: "s.EnumField",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingInterfaceFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WriteInterface(s.IntField.PassMessagePipe()); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isInterface:   true,
		identifier:    "s.IntField",
		writeFunction: "WriteInterface",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableInterfaceFieldEncoding(t *testing.T) {
	expected := `if s.IntField == nil {
	encoder.WriteInvalidInterface()
} else {
	if err := encoder.WriteInterface(s.IntField.PassMessagePipe()); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isInterface:   true,
		isNullable:    true,
		identifier:    "s.IntField",
		writeFunction: "WriteInterface",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingInterfaceRequestFieldEncoding(t *testing.T) {
	expected := `if err := encoder.WriteHandle(s.IntField.PassMessagePipe()); err != nil {
	return err
}`

	encodingInfo := mockEncodingInfo{
		isInterface:        true,
		isInterfaceRequest: true,
		identifier:         "s.IntField",
		writeFunction:      "WriteHandle",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}

func TestEncodingNullableInterfaceRequestFieldEncoding(t *testing.T) {
	expected := `if s.IntField == nil {
	encoder.WriteInvalidHandle()
} else {
	if err := encoder.WriteHandle(s.IntField.PassMessagePipe()); err != nil {
		return err
	}
}`

	encodingInfo := mockEncodingInfo{
		isInterface:        true,
		isInterfaceRequest: true,
		isNullable:         true,
		identifier:         "s.IntField",
		writeFunction:      "WriteHandle",
	}

	check(t, expected, "FieldEncodingTmpl", encodingInfo)
}
