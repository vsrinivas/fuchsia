// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"testing"

	"mojom/generators/go/translator"
)

func TestUnionInterfaceDecl(t *testing.T) {
	expected := `type SomeUnion interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__SomeUnionReflect)
	Encode(encoder *bindings.Encoder) error
}

type __SomeUnionReflect struct {
	Alpha string
	Beta  uint32
}`

	union := translator.UnionTemplate{
		Name: "SomeUnion",
		Fields: []translator.UnionFieldTemplate{
			{Name: "Alpha", Type: "string"},
			{Name: "Beta", Type: "uint32"},
		},
	}

	check(t, expected, "UnionInterfaceDecl", union)
}

func TestUnionFieldDecl(t *testing.T) {
	expected := `type SomeUnionAlpha struct{ Value string }

func (u *SomeUnionAlpha) Tag() uint32                  { return 5 }
func (u *SomeUnionAlpha) Interface() interface{}       { return u.Value }
func (u *SomeUnionAlpha) __Reflect(__SomeUnionReflect) {}`

	union := translator.UnionTemplate{Name: "SomeUnion"}

	field := translator.UnionFieldTemplate{
		Name:  "Alpha",
		Type:  "string",
		Tag:   5,
		Union: &union,
	}

	check(t, expected, "UnionFieldDecl", field)
}

func TestUnionFieldEncode(t *testing.T) {
	expected := `func (u *SomeUnionSomeField) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteUint32(u.Value); err != nil {
		return err
	}
	
	encoder.FinishWritingUnionValue()
	return nil
}`

	union := translator.UnionTemplate{Name: "SomeUnion"}

	info := mockEncodingInfo{
		isSimple:      true,
		identifier:    "u.Value",
		writeFunction: "WriteUint32",
	}

	field := translator.UnionFieldTemplate{
		Name:         "SomeField",
		Type:         "string",
		Tag:          5,
		Union:        &union,
		EncodingInfo: info,
	}

	check(t, expected, "UnionFieldEncode", field)
}

func TestUnionFieldDecode(t *testing.T) {
	expected := `func (u *SomeUnionSomeField) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadUint32()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}`

	union := translator.UnionTemplate{Name: "SomeUnion"}

	info := mockEncodingInfo{
		isSimple:      true,
		identifier:    "u.Value",
		writeFunction: "WriteUint32",
		readFunction:  "ReadUint32",
	}

	field := translator.UnionFieldTemplate{
		Name:         "SomeField",
		Type:         "string",
		Tag:          5,
		Union:        &union,
		EncodingInfo: info,
	}

	check(t, expected, "UnionFieldDecode", field)
}

func TestUnionDecode(t *testing.T) {
	expected := `func DecodeSomeUnion(decoder *bindings.Decoder) (SomeUnion, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value SomeUnionField1
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value SomeUnionField2
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &SomeUnionUnknown{tag: tag}, nil
}`

	field1 := translator.UnionFieldTemplate{
		Name: "Field1",
		Tag:  0,
	}

	field2 := translator.UnionFieldTemplate{
		Name: "Field2",
		Tag:  1,
	}

	union := translator.UnionTemplate{
		Name: "SomeUnion",
		Fields: []translator.UnionFieldTemplate{
			field1,
			field2,
		},
	}

	check(t, expected, "UnionDecode", union)
}
