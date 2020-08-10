// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"gidl/ir"
	"strings"
	"testing"

	fidlir "fidl/compiler/backend/types"

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
		{gidl: `"\x00"`, expectedValue: "\x00"},
		{gidl: `"\""`, expectedValue: "\""},
		{gidl: `true`, expectedValue: true},
		{gidl: `null`, expectedValue: nil},
		{gidl: `#0`, expectedValue: ir.Handle(0)},
		{gidl: `#123`, expectedValue: ir.Handle(123)},
		{gidl: `SomeRecord {}`, expectedValue: ir.Record{
			Name: "SomeRecord",
		}},
		{gidl: `SomeRecord { the_field: 5, }`, expectedValue: ir.Record{
			Name: "SomeRecord",
			Fields: []ir.Field{
				{
					Key: ir.FieldKey{
						Name: "the_field",
					},
					Value: uint64(5),
				},
			},
		}},
		{gidl: `SomeRecord { the_field: null, }`, expectedValue: ir.Record{
			Name: "SomeRecord",
			Fields: []ir.Field{
				{
					Key: ir.FieldKey{
						Name: "the_field",
					},
					Value: nil,
				},
			},
		}},
		{gidl: `SomeRecord { 0x01020304: { bytes = [1, 2] }, }`, expectedValue: ir.Record{
			Name: "SomeRecord",
			Fields: []ir.Field{
				{
					Key: ir.FieldKey{
						UnknownOrdinal: 0x01020304,
					},
					Value: ir.UnknownData{
						Bytes: []byte{1, 2},
					},
				},
			},
		}},
		{gidl: `SomeRecord { f1: 0x01, }`, expectedValue: ir.Record{
			Name: "SomeRecord",
			Fields: []ir.Field{
				{
					Key: ir.FieldKey{
						Name: "f1",
					},
					Value: uint64(1),
				},
			},
		}},
		{gidl: `SomeRecord {
			the_field: SomeNestedRecord {
				foo: 5,
				bar: 7,
			},
		}`, expectedValue: ir.Record{
			Name: "SomeRecord",
			Fields: []ir.Field{
				{
					Key: ir.FieldKey{
						Name: "the_field",
					},
					Value: ir.Record{
						Name: "SomeNestedRecord",
						Fields: []ir.Field{
							{
								Key: ir.FieldKey{
									Name: "foo",
								},
								Value: uint64(5),
							},
							{
								Key: ir.FieldKey{
									Name: "bar",
								},
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
		{gidl: `[null,]`, expectedValue: []interface{}{nil}},
	}
	for _, tc := range testCases {
		t.Run(tc.gidl, func(t *testing.T) {
			p := NewParser("", strings.NewReader(tc.gidl), Config{})
			value, err := p.parseValue()
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestFailsParseValues(t *testing.T) {
	type testCase struct {
		gidl                string
		expectedErrorSubstr string
	}
	testCases := []testCase{
		{gidl: `"`, expectedErrorSubstr: "improperly escaped string"},
		{gidl: `"\xwrong"`, expectedErrorSubstr: "improperly escaped string"},
		{gidl: `#-1`, expectedErrorSubstr: `want "<text>", got "-"`},
		{gidl: `SomeRecord { 0x01020304: 5, }`, expectedErrorSubstr: "unexpected tokenKind"},
	}
	for _, tc := range testCases {
		t.Run(tc.gidl, func(t *testing.T) {
			p := NewParser("", strings.NewReader(tc.gidl), Config{})
			_, err := p.parseValue()
			checkFailure(t, err, tc.expectedErrorSubstr)
		})
	}
}

func TestParseBytes(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue []ir.Encoding
	}
	testCases := []testCase{
		// empty
		{
			gidl: `{ alpha = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      nil,
				},
			},
		},
		// base 10
		{
			gidl: `{ alpha = [1, 2, 3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{1, 2, 3},
				},
			},
		},
		// base 16
		{
			gidl: `{ alpha = [0x0, 0xff, 0xA, 0x0a, 7] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0, 255, 10, 10, 7},
				},
			},
		},
		// character codes
		{
			gidl: `{ alpha = ['h', 'e', 'l', 'l', 'o'] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{'h', 'e', 'l', 'l', 'o'},
				},
			},
		},
		// positive number
		{
			gidl: `{ alpha = [num(2147483647):4] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0xff, 0xff, 0xff, 0x7f},
				},
			},
		},
		// negative number
		{
			gidl: `{ alpha = [num(-32768):2] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0x00, 0x80},
				},
			},
		},
		// padding
		{
			gidl: `{ alpha = [padding:3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0, 0, 0},
				},
			},
		},
		// repeat a byte
		{
			gidl: `{ alpha = [repeat(0x33):3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0x33, 0x33, 0x33},
				},
			},
		},
		// multiple byte generators in same list
		{
			gidl: `{ alpha = [num(127):2, repeat(0x33):3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0x7f, 0x00, 0x33, 0x33, 0x33},
				},
			},
		},
		// mix plain bytes, characters, and generators
		{
			gidl: `{ alpha = [num(127):2, 255, padding:1, 'A'] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{0x7f, 0x00, 0xff, 0x00, 'A'},
				},
			},
		},
		// trailing comma allowed
		{
			gidl: `{ alpha = [1,2,] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{1, 2},
				},
			},
		},
		// multiple wire formats, same bytes (empty), ordering 1
		{
			gidl: `{ alpha, beta = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      nil,
				},
				{
					WireFormat: "beta",
					Bytes:      nil,
				},
			},
		},
		// multiple wire formats, same bytes (empty), ordering 2
		{
			gidl: `{ beta, alpha = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "beta",
					Bytes:      nil,
				},
				{
					WireFormat: "alpha",
					Bytes:      nil,
				},
			},
		},
		// multiple wire formats, same bytes (non-empty)
		{
			gidl: `{ alpha, beta = [1, 2, 3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{1, 2, 3},
				},
				{
					WireFormat: "beta",
					Bytes:      []byte{1, 2, 3},
				},
			},
		},
		// multiple wire formats, different bytes
		{
			gidl: `{
				alpha = [1, 2, 3],
				beta = [repeat(4):3],
			}`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Bytes:      []byte{1, 2, 3},
				},
				{
					WireFormat: "beta",
					Bytes:      []byte{4, 4, 4},
				},
			},
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{
			WireFormats: []ir.WireFormat{"alpha", "beta"},
		})
		value, err := p.parseByteSection()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseBytesFailures(t *testing.T) {
	type testCase struct {
		gidl         string
		errSubstring string
	}
	testCases := []testCase{
		{
			gidl:         `{}`,
			errSubstring: "no bytes",
		},
		{
			gidl:         `{ alpha = [PADDING:0] }`,
			errSubstring: "invalid byte syntax",
		},
		{
			gidl:         `{ alpha = [thisisnotagenerator(1):0] }`,
			errSubstring: "invalid byte syntax",
		},
		{
			gidl:         `{ alpha = [padding:0] }`,
			errSubstring: "non-zero",
		},
		{
			gidl:         `{ alpha = [num(65536):2] }`,
			errSubstring: "exceeds byte size",
		},
		{
			gidl:         `{ alpha = [num(-32769):2] }`,
			errSubstring: "exceeds byte size",
		},
		{
			gidl:         `{ alpha, alpha = [] }`,
			errSubstring: "duplicate wire format",
		},
		{
			gidl:         `{ alpha = [], beta, alpha = [] }`,
			errSubstring: "duplicate wire format",
		},
		{
			gidl:         `{ this_is_not_a_wire_format = [] }`,
			errSubstring: "invalid wire format",
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{
			WireFormats: []ir.WireFormat{"alpha", "beta"},
		})
		_, err := p.parseByteSection()
		t.Run(tc.gidl, func(t *testing.T) {
			if err == nil {
				t.Fatalf("error was expected, but no error was returned")
			}
			if !strings.Contains(err.Error(), tc.errSubstring) {
				t.Errorf("expected error containing %q, but got %q", tc.errSubstring, err.Error())
			}
		})
	}
}

func TestParseHandles(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue []ir.Encoding
	}
	testCases := []testCase{
		// no entries
		{
			gidl:          `{}`,
			expectedValue: nil,
		},
		// empty list
		{
			gidl: `{ alpha = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    nil,
				},
			},
		},
		// one handle
		{
			gidl: `{ alpha = [#0] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    []ir.Handle{0},
				},
			},
		},
		// several handles
		{
			gidl: `{ alpha = [#42, #1, #3] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    []ir.Handle{42, 1, 3},
				},
			},
		},
		// trailing comma allowed
		{
			gidl: `{ alpha = [#0,#1,] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    []ir.Handle{0, 1},
				},
			},
		},
		// multiple wire formats, same handles (empty), ordering 1
		{
			gidl: `{ alpha, beta = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    nil,
				},
				{
					WireFormat: "beta",
					Handles:    nil,
				},
			},
		},
		// multiple wire formats, same handles (empty), ordering 2
		{
			gidl: `{ beta, alpha = [] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "beta",
					Handles:    nil,
				},
				{
					WireFormat: "alpha",
					Handles:    nil,
				},
			},
		},
		// multiple wire formats, same handles (non-empty)
		{
			gidl: `{ alpha, beta = [#0, #1] }`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    []ir.Handle{0, 1},
				},
				{
					WireFormat: "beta",
					Handles:    []ir.Handle{0, 1},
				},
			},
		},
		// multiple wire formats, different handles
		{
			gidl: `{
				alpha = [#0, #1],
				beta = [#1, #0],
			}`,
			expectedValue: []ir.Encoding{
				{
					WireFormat: "alpha",
					Handles:    []ir.Handle{0, 1},
				},
				{
					WireFormat: "beta",
					Handles:    []ir.Handle{1, 0},
				},
			},
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{
			WireFormats: []ir.WireFormat{"alpha", "beta"},
		})
		value, err := p.parseHandleSection()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseHandlesFailures(t *testing.T) {
	type testCase struct {
		gidl         string
		errSubstring string
	}
	testCases := []testCase{
		{
			gidl:         `{ alpha = [0] }`,
			errSubstring: `want "#", got "<text>"`,
		},
		{
			gidl:         `{ alpha = [#-1] }`,
			errSubstring: `want "<text>", got "-"`,
		},
		{
			gidl:         `{ alpha, alpha = [] }`,
			errSubstring: "duplicate wire format",
		},
		{
			gidl:         `{ alpha = [], beta, alpha = [] }`,
			errSubstring: "duplicate wire format",
		},
		{
			gidl:         `{ this_is_not_a_wire_format = [] }`,
			errSubstring: "invalid wire format",
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{
			WireFormats: []ir.WireFormat{"alpha", "beta"},
		})
		_, err := p.parseHandleSection()
		t.Run(tc.gidl, func(t *testing.T) {
			if err == nil {
				t.Fatalf("error was expected, but no error was returned")
			}
			if !strings.Contains(err.Error(), tc.errSubstring) {
				t.Errorf("expected error containing %q, but got %q", tc.errSubstring, err.Error())
			}
		})
	}
}

func TestParseHandleDefs(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue []ir.HandleDef
	}
	testCases := []testCase{
		// no entries
		{
			gidl:          `{}`,
			expectedValue: nil,
		},
		// one handle
		{
			gidl: `{ #0 = event() }`,
			expectedValue: []ir.HandleDef{
				{Subtype: fidlir.Event},
			},
		},
		// several handles
		{
			gidl: `{ #0 = event(), #1 = event(), #2 = event() }`,
			expectedValue: []ir.HandleDef{
				{Subtype: fidlir.Event},
				{Subtype: fidlir.Event},
				{Subtype: fidlir.Event},
			},
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{})
		value, err := p.parseHandleDefSection()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseHandleDefsFailures(t *testing.T) {
	type testCase struct {
		gidl         string
		errSubstring string
	}
	testCases := []testCase{
		{
			gidl:         `{ #1 = event() }`,
			errSubstring: `want #0, got #1`,
		},
		{
			gidl:         `{ #0 = event(), #2 = event() }`,
			errSubstring: `want #1, got #2`,
		},
		{
			gidl:         `{ #0 = invalidsubtype() }`,
			errSubstring: "invalid handle subtype",
		},
		{
			gidl:         `{ #0 = event }`,
			errSubstring: `want "(", got "}"`,
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{})
		_, err := p.parseHandleDefSection()
		t.Run(tc.gidl, func(t *testing.T) {
			if err == nil {
				t.Fatalf("error was expected, but no error was returned")
			}
			if !strings.Contains(err.Error(), tc.errSubstring) {
				t.Errorf("expected error containing %q, but got %q", tc.errSubstring, err.Error())
			}
		})
	}
}

func TestParseUnknownData(t *testing.T) {
	type testCase struct {
		gidl          string
		expectedValue ir.UnknownData
	}
	testCases := []testCase{
		// empty
		{
			gidl: `{ bytes = [] }`,
			expectedValue: ir.UnknownData{
				Bytes: nil,
			},
		},
		// bytes
		{
			gidl: `{ bytes = [0xde, 0xad, 0xbe, 0xef] }`,
			expectedValue: ir.UnknownData{
				Bytes: []byte{0xde, 0xad, 0xbe, 0xef},
			},
		},
		// bytes and handles
		{
			gidl: `{ bytes = [0xde, 0xad, 0xbe, 0xef], handles = [#3, #4] }`,
			expectedValue: ir.UnknownData{
				Bytes:   []byte{0xde, 0xad, 0xbe, 0xef},
				Handles: []ir.Handle{3, 4},
			},
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{})
		value, err := p.parseUnknownData()
		t.Run(tc.gidl, func(t *testing.T) {
			checkMatch(t, value, tc.expectedValue, err)
		})
	}
}

func TestParseUnknownDataFailures(t *testing.T) {
	type testCase struct {
		gidl         string
		errSubstring string
	}
	testCases := []testCase{
		{
			gidl:         `{}`,
			errSubstring: "missing required parameter 'bytes'",
		},
		{
			gidl:         `{ value = Foo { bar: 3 } }`,
			errSubstring: "parameter 'value' does not apply to unknown data",
		},
		{
			gidl:         `{ bytes = [1, 2, 3], bytes = [4, 5] }`,
			errSubstring: "duplicate parameter 'bytes' found",
		},
	}
	for _, tc := range testCases {
		p := NewParser("", strings.NewReader(tc.gidl), Config{})
		_, err := p.parseUnknownData()
		t.Run(tc.gidl, func(t *testing.T) {
			if err == nil {
				t.Fatalf("error was expected, but no error was returned")
			}
			if !strings.Contains(err.Error(), tc.errSubstring) {
				t.Errorf("expected error containing %q, but got %q", tc.errSubstring, err.Error())
			}
		})
	}
}

func TestParseSuccessCase(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "first",
						},
						Value: "four",
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			}},
		}},
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "first",
						},
						Value: "four",
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			}},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseEncodeSuccessCase(t *testing.T) {
	gidl := `
	encode_success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "first",
						},
						Value: "four",
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			}},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseDecodeSuccessCase(t *testing.T) {
	gidl := `
	decode_success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "first",
						},
						Value: "four",
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes: []byte{
					0, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			}},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseEncodeFailureCase(t *testing.T) {
	gidl := `
	encode_failure("OneStringOfMaxLengthFive-too-long") {
		value = OneStringOfMaxLengthFive {
			the_string: "bonjour", // 6 characters
		},
		err = STRING_TOO_LONG,
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		EncodeFailure: []ir.EncodeFailure{{
			Name:        "OneStringOfMaxLengthFive-too-long",
			WireFormats: []ir.WireFormat{ir.V1WireFormat},
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "the_string",
						},
						Value: "bonjour",
					},
				},
			},
			Err: "STRING_TOO_LONG",
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseDecodeFailureCase(t *testing.T) {
	gidl := `
	decode_failure("OneStringOfMaxLengthFive-wrong-length") {
		type = TypeName,
		bytes = {
			v1 = [
				1, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				// one character missing
			],
		},
		err = STRING_TOO_LONG,
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		DecodeFailure: []ir.DecodeFailure{{
			Name: "OneStringOfMaxLengthFive-wrong-length",
			Type: "TypeName",
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes: []byte{
					1, 0, 0, 0, 0, 0, 0, 0, // length
					255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				},
			}},
			Err: "STRING_TOO_LONG",
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseBenchmarkCase(t *testing.T) {
	gidl := `
	benchmark("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
	}`
	all, err := parse(gidl)
	expectedAll := ir.All{
		Benchmark: []ir.Benchmark{{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Record{
				Name: "OneStringOfMaxLengthFive",
				Fields: []ir.Field{
					{
						Key: ir.FieldKey{
							Name: "first",
						},
						Value: "four",
					},
				},
			},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseSucceedsBindingsAllowlistAndDenylist(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
		bindings_allowlist = [go, rust],
		bindings_denylist = [dart],
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		Languages:   []string{"go", "rust", "dart"},
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
	var all ir.All
	err := p.parseSection(&all)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Record{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Key: ir.FieldKey{
								Name: "first",
							},
							Value: "four",
						},
					},
				},
				Encodings: []ir.Encoding{{
					WireFormat: ir.V1WireFormat,
					Bytes: []byte{
						0, 0, 0, 0, 0, 0, 0, 0, // length
						255, 255, 255, 255, 255, 255, 255, 255, // alloc present
					},
				}},
				BindingsAllowlist: &ir.LanguageList{"go", "rust"},
				BindingsDenylist:  &ir.LanguageList{"dart"},
			},
		},
		DecodeSuccess: []ir.DecodeSuccess{
			{
				Name: "OneStringOfMaxLengthFive-empty",
				Value: ir.Record{
					Name: "OneStringOfMaxLengthFive",
					Fields: []ir.Field{
						{
							Key: ir.FieldKey{
								Name: "first",
							},
							Value: "four",
						},
					},
				},
				Encodings: []ir.Encoding{{
					WireFormat: ir.V1WireFormat,
					Bytes: []byte{
						0, 0, 0, 0, 0, 0, 0, 0, // length
						255, 255, 255, 255, 255, 255, 255, 255, // alloc present
					},
				}},
				BindingsAllowlist: &ir.LanguageList{"go", "rust"},
				BindingsDenylist:  &ir.LanguageList{"dart"},
			},
		},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseFailsBindingsAllowlist(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
		bindings_allowlist = [go, rust],
		bindings_denylist = [dart],
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		Languages:   []string{"rust", "dart"},
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
	var all ir.All
	err := p.parseSection(&all)
	checkFailure(t, err, "invalid language 'go'")
}

func TestParseFailsBindingsDenylist(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
		bindings_allowlist = [go, rust],
		bindings_denylist = [dart],
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		Languages:   []string{"rust", "go"},
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
	var all ir.All
	err := p.parseSection(&all)
	checkFailure(t, err, "invalid language 'dart'")
}

func TestParseSucceedsMultipleWireFormats(t *testing.T) {
	gidl := `
	success("MultipleWireFormats") {
		value = MultipleWireFormats {},
		bytes = {
			zero = [0],
			one = [1],
		}
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		WireFormats: []ir.WireFormat{"zero", "one"},
	})
	var all ir.All
	err := p.parseSection(&all)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "MultipleWireFormats",
			Value: ir.Record{
				Name:   "MultipleWireFormats",
				Fields: []ir.Field(nil),
			},
			Encodings: []ir.Encoding{
				{
					WireFormat: "zero",
					Bytes:      []byte{0},
				},
				{
					WireFormat: "one",
					Bytes:      []byte{1},
				},
			},
		}},
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "MultipleWireFormats",
			Value: ir.Record{
				Name:   "MultipleWireFormats",
				Fields: []ir.Field(nil),
			},
			Encodings: []ir.Encoding{
				{
					WireFormat: "zero",
					Bytes:      []byte{0},
				},
				{
					WireFormat: "one",
					Bytes:      []byte{1},
				},
			},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseFailsExtraKind(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		type = Type,
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
		bytes = {
			v1 = [
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			],
		},
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "'type' does not apply")
}

func TestParseFailsMissingKind(t *testing.T) {
	gidl := `
	success("OneStringOfMaxLengthFive-empty") {
		value = OneStringOfMaxLengthFive {
			first: "four",
		},
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "missing required parameter 'bytes'")
}

func TestParseFailsUnknownErrorCode(t *testing.T) {
	gidl := `
	encode_failure("OneStringOfMaxLengthFive-too-long") {
		value = OneStringOfMaxLengthFive {
			the_string: "bonjour",
		},
		err = UNKNOWN_ERROR_CODE,
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "unknown error code")
}

func TestParseFailsNoBytes(t *testing.T) {
	gidl := `
	success("NoBytes") {
		value = NoBytes {},
		bytes = {},
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "no bytes")
}

func TestParseFailsDuplicateWireFormat(t *testing.T) {
	gidl := `
	success("DuplicateWireFormat") {
		value = DuplicateWireFormat {},
		bytes = {
			v1 = [],
			v1 = [],
		}
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "duplicate wire format")
}

func TestParseSucceedsHandles(t *testing.T) {
	gidl := `
	success("HasHandles") {
		handle_defs = {
			#0 = event(),
		},
		value = HasHandles { h: #0 },
		bytes = {
			v1 = [ repeat(0xff):4, padding:4 ],
		},
		handles = {
			v1 = [ #0 ],
		},
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
	var all ir.All
	err := p.parseSection(&all)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "HasHandles",
			Value: ir.Record{
				Name: "HasHandles",
				Fields: []ir.Field{
					{
						Key:   ir.FieldKey{Name: "h"},
						Value: ir.Handle(0),
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes:      []byte{255, 255, 255, 255, 0, 0, 0, 0},
				Handles:    []ir.Handle{0},
			}},
			HandleDefs: []ir.HandleDef{
				{Subtype: fidlir.Event},
			},
		}},
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "HasHandles",
			Value: ir.Record{
				Name: "HasHandles",
				Fields: []ir.Field{
					{
						Key:   ir.FieldKey{Name: "h"},
						Value: ir.Handle(0),
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes:      []byte{255, 255, 255, 255, 0, 0, 0, 0},
				Handles:    []ir.Handle{0},
			}},
			HandleDefs: []ir.HandleDef{
				{Subtype: fidlir.Event},
			},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseSucceedsHandlesDefinedAfter(t *testing.T) {
	gidl := `
	success("HasHandles") {
		value = HasHandles { h: #0 },
		bytes = {
			v1 = [ repeat(0xff):4, padding:4 ],
		},
		handles = {
			v1 = [ #0 ],
		},
		// handle_defs at the end.
		handle_defs = {
			#0 = event(),
		},
	}`
	p := NewParser("", strings.NewReader(gidl), Config{
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
	var all ir.All
	err := p.parseSection(&all)
	expectedAll := ir.All{
		EncodeSuccess: []ir.EncodeSuccess{{
			Name: "HasHandles",
			Value: ir.Record{
				Name: "HasHandles",
				Fields: []ir.Field{
					{
						Key:   ir.FieldKey{Name: "h"},
						Value: ir.Handle(0),
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes:      []byte{255, 255, 255, 255, 0, 0, 0, 0},
				Handles:    []ir.Handle{0},
			}},
			HandleDefs: []ir.HandleDef{
				{Subtype: fidlir.Event},
			},
		}},
		DecodeSuccess: []ir.DecodeSuccess{{
			Name: "HasHandles",
			Value: ir.Record{
				Name: "HasHandles",
				Fields: []ir.Field{
					{
						Key:   ir.FieldKey{Name: "h"},
						Value: ir.Handle(0),
					},
				},
			},
			Encodings: []ir.Encoding{{
				WireFormat: ir.V1WireFormat,
				Bytes:      []byte{255, 255, 255, 255, 0, 0, 0, 0},
				Handles:    []ir.Handle{0},
			}},
			HandleDefs: []ir.HandleDef{
				{Subtype: fidlir.Event},
			},
		}},
	}
	checkMatch(t, all, expectedAll, err)
}

func TestParseFailsUndefinedHandleInValue(t *testing.T) {
	gidl := `
	success("UndefinedHandleInValue") {
		value = Value { h: #0 },
		bytes = { v1 = [] },
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "missing definition for handle #0")
}

func TestParseFailsUndefinedHandleInHandles(t *testing.T) {
	gidl := `
	success("UndefinedHandleInHandles") {
		value = Value {},
		bytes = { v1 = [] },
		handles = { v1 = [ #0 ] },
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "missing definition for handle #0")
}

func TestParseFailsHandleUsedTwiceInValue(t *testing.T) {
	gidl := `
	success("HandleUsedTwiceInValue") {
		handle_defs = { #0 = event() },
		value = Value { h0: #0, h1: #0 },
		bytes = { v1 = [] },
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "handle #0 used more than once in 'value' section")
}

func TestParseFailsHandleUsedTwiceInHandles(t *testing.T) {
	gidl := `
	success("HandleUsedTwiceInHandles") {
		handle_defs = { #0 = event() },
		value = Value {},
		bytes = { v1 = [] },
		handles = { v1 = [ #0, #0 ] },
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "handle #0 used more than once in 'handles' section")
}

func TestParseFailsUnusedHandle(t *testing.T) {
	gidl := `
	success("UnusedHandle") {
		handle_defs = { #0 = event() },
		value = Value {},
		bytes = { v1 = [] },
	}`
	_, err := parse(gidl)
	checkFailure(t, err, "unused handle #0")
}

func parse(gidlInput string) (ir.All, error) {
	p := NewParser("", strings.NewReader(gidlInput), Config{
		WireFormats: []ir.WireFormat{ir.V1WireFormat},
	})
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
			p := NewParser("", strings.NewReader(input), Config{})
			for index, expected := range expecteds {
				actual, err := p.nextToken()
				if err != nil {
					t.Fatalf("unexpected error reading next token: %s", err)
				}
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
