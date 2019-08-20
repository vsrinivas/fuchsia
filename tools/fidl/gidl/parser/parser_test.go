// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"strings"
	"testing"

	"gidl/ir"

	"github.com/google/go-cmp/cmp"
)

func TestParseValues(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue interface{}
	}
	testCases := []testCase{
		{gidl: `1`, expectedValue: uint64(1)},
		{gidl: `-78`, expectedValue: int64(-78)},
		{gidl: `3.14`, expectedValue: float64(3.14)},
		{gidl: `-3.14`, expectedValue: float64(-3.14)},
		{gidl: `"hello"`, expectedValue: "hello"},
		{gidl: `true`, expectedValue: true},
		{gidl: `SomeObject {}`, expectedValue: ir.Object{
			Name: "SomeObject",
		}},
		{gidl: `SomeObject { the_field: 5, }`, expectedValue: ir.Object{
			Name: "SomeObject",
			Fields: []ir.Field{
				{
					Name:  "the_field",
					Value: uint64(5),
				},
			},
		}},
		{gidl: `SomeObject {
			the_field: SomeNestedObject {
				foo: 5,
				bar: 7,
			},
		}`, expectedValue: ir.Object{
			Name: "SomeObject",
			Fields: []ir.Field{
				{
					Name: "the_field",
					Value: ir.Object{
						Name: "SomeNestedObject",
						Fields: []ir.Field{
							{
								Name:  "foo",
								Value: uint64(5),
							},
							{
								Name:  "bar",
								Value: uint64(7),
							},
						},
					},
				},
			},
		}},
		{gidl: `[]`, expectedValue: []interface{}(nil)},
		{gidl: `[1,]`, expectedValue: []interface{}{uint64(1)}},
		{gidl: `[1,"hello",true,]`, expectedValue: []interface{}{uint64(1), "hello", true}},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl))
		value, err := p.parseValue()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseBytes(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue []byte
	}
	testCases := []testCase{
		{
			gidl: `
		{
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}`,
			expectedValue: []byte{
				0, 0, 0, 0, 0, 0, 0, 0,
				255, 255, 255, 255, 255, 255, 255, 255,
			},
		},
		{
			gidl: `
		{
			0x0, 0xff, 0xA, 0x0a, 7,
		}`,
			expectedValue: []byte{
				0, 255, 10, 10, 7,
			},
		},
		{
			gidl: `
	{
		'h', 'e', 'l', 'l', 'o',
	}`,
			expectedValue: []byte{
				'h', 'e', 'l', 'l', 'o',
			},
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl))
		value, err := p.parseBytes()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseSuccessCase(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
		bytes = {
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		Success: []ir.Success{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Name:  "first",
						Value: "four",
					},
				},
			},
			Bytes: []byte{
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			},
		},
		},
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Name:  "first",
						Value: "four",
					},
				},
			},
			Bytes: []byte{
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			},
		},
		},
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Name:  "first",
						Value: "four",
					},
				},
			},
			Bytes: []byte{
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			},
		},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseEncodeSuccessCase(t *testing.T) {
	gidl := `
	encode_success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
		bytes = {
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Object{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Name:  "first",
							Value: "four",
						},
					},
				},
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseDecodeSuccessCase(t *testing.T) {
	gidl := `
	decode_success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
		bytes = {
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Name:  "first",
						Value: "four",
					},
				},
			},
			Bytes: []byte{
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			},
		},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseEncodeFailureCase(t *testing.T) {
	gidl := `
	encode_failure("OneStringOfMaxLengthFive-too-long") {
		value = OneStringOfMaxLengthFive {
			the_string: "bonjour", // 6 characters
		}
		err = STRING_TOO_LONG
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		EncodeFailure: []ir.EncodeFailure{{
			Name: "OneStringOfMaxLengthFive-too-long",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Name:  "the_string",
						Value: "bonjour",
					},
				},
			},
			Err: "STRING_TOO_LONG",
		},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseDecodeFailureCase(t *testing.T) {
	gidl := `
	decode_failure("OneStringOfMaxLengthFive-wrong-length") {
		type = TypeName
		bytes = {
			1, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			// one character missing
		}
		err = STRING_TOO_LONG
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{DecodeFailure: []ir.DecodeFailure{{
		Name: "OneStringOfMaxLengthFive-wrong-length",
		Type: "TypeName",
		Bytes: []byte{
			1, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			// one character missing
		},
		Err: "STRING_TOO_LONG",
	},
	},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseSucceedsBindingsAllowlistAndDenylist(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
		bytes = {
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}
		bindings_allowlist = [go, rust,]
		bindings_denylist = [go,]
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		Success: []ir.Success{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Object{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Name:  "first",
							Value: "four",
						},
					},
				},
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
				BindingsAllowlist: &[]string{"go", "rust"},
				BindingsDenylist:  &[]string{"go"},
			},
		},
		EncodeSuccess: []ir.EncodeSuccess{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Object{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Name:  "first",
							Value: "four",
						},
					},
				},
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
				BindingsAllowlist: &[]string{"go", "rust"},
				BindingsDenylist:  &[]string{"go"},
			},
		},
		DecodeSuccess: []ir.DecodeSuccess{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Object{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Name:  "first",
							Value: "four",
						},
					},
				},
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
				BindingsAllowlist: &[]string{"go", "rust"},
				BindingsDenylist:  &[]string{"go"},
			},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseFailsExtraKind(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		type = Type
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
		bytes = {
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "'type' does not apply")
}

func TestParseFailsMissingKind(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		}
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "missing required parameter 'bytes'")
}

func TestParseFailsUnknownErrorCode(t *testing.T) {
	input := `
	encode_failure("OneStringOfMaxLengthFive-too-long") {
		value = OneStringOfMaxLengthFive {
			the_string: "bonjour",
		}
		err = UNKNOWN_ERROR_CODE
	}`
	p := NewParser("", strings.NewReader(input))
	var all ir.All
	if err := p.parseSection(&all); err == nil || !strings.Contains(err.Error(), "unknown error code") {
		t.Errorf("expected 'unknown error code' error, but got %v", err)
	}
}

func parse(gidlInput string) (ir.All, error) {
	p := NewParser("", strings.NewReader(gidlInput))
	var all ir.All
	err := p.parseSection(&all)
	return all, err
}

func checkMatch(t *testing.T, actual, expected interface{}, err error) {
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("expected: %T %v", expected, expected)
	t.Logf("actual: %T %v", actual, actual)
	if diff := cmp.Diff(expected, actual); diff != "" {
		t.Errorf("expected != actual (-want +got)\n%s", diff)
	}
}

func checkFailure(t *testing.T, err error, errorSubstr string) {
	if err == nil {
		t.Errorf("expected error: %s", errorSubstr)
		return
	}
	if !strings.Contains(err.Error(), errorSubstr) {
		t.Errorf("expected error containing %s, instead got %s", errorSubstr, err.Error())
	}
}

func TestTokenizationSuccess(t *testing.T) {
	cases := map[string][]token{
		"1,2,3": {
			{tText, "1", 1, 1},
			{tComma, ",", 1, 2},
			{tText, "2", 1, 3},
			{tComma, ",", 1, 4},
			{tText, "3", 1, 5},
			{tEof, "", 0, 0},
		},
		"'1', '22'": {
			{tText, "'1'", 1, 1},
			{tComma, ",", 1, 4},
			{tText, "'22'", 1, 6},
		},
	}
	for input, expecteds := range cases {
		t.Run(input, func(t *testing.T) {
			p := NewParser("", strings.NewReader(input))
			for index, expected := range expecteds {
				actual := p.nextToken()
				if actual != expected {
					t.Fatalf(
						"#%d: expected %s (line: %d col: %d), actual %s (line: %d col: %d)", index,
						expected, expected.line, expected.column,
						actual, actual.line, actual.column)
				}
				t.Logf("#%d: %s", index, expected)
			}
		})
	}
}

func TestVariousStringFuncs(t *testing.T) {
	cases := map[fmt.Stringer]string{
		tComma:                          ",",
		tEof:                            "<eof>",
		token{tComma, "whatever", 0, 0}: ",",
		token{tText, "me me me", 0, 0}:  "me me me",
		isValue:                         "value",
	}
	for value, expected := range cases {
		actual := value.String()
		if expected != actual {
			t.Errorf("%v: expected %s, actual %s", value, expected, actual)
		}
	}
}
