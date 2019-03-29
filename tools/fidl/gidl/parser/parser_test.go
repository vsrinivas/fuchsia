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

func TestParseValue(t *testing.T) {
	parsingToCheck{
		t: t,
		fn: func(p *Parser) (interface{}, error) {
			return p.parseValue()
		},
	}.checkSuccess(map[string]interface{}{
		`1`:       uint64(1),
		`-78`:     int64(-78),
		`3.14`:    float64(3.14),
		`-3.14`:   float64(-3.14),
		`"hello"`: "hello",
		`true`:    true,
		`SomeObject {}`: ir.Object{
			Name: "SomeObject",
		},
		`SomeObject { the_field: 5, }`: ir.Object{
			Name: "SomeObject",
			Fields: map[string]ir.Value{
				"the_field": uint64(5),
			},
		},
		`SomeObject {
			the_field: SomeNestedObject {
				foo: 5,
				bar: 7,
			},
		}`: ir.Object{
			Name: "SomeObject",
			Fields: map[string]ir.Value{
				"the_field": ir.Object{
					Name: "SomeNestedObject",
					Fields: map[string]ir.Value{
						"foo": uint64(5),
						"bar": uint64(7),
					},
				},
			},
		},
		`[]`:                []interface{}(nil),
		`[1,]`:              []interface{}{uint64(1)},
		`[1,"hello",true,]`: []interface{}{uint64(1), "hello", true},
	})
}

func TestParseBytes(t *testing.T) {
	parsingToCheck{
		t: t,
		fn: func(p *Parser) (interface{}, error) {
			return p.parseBytes()
		},
	}.checkSuccess(map[string]interface{}{
		`
		{
			0, 0, 0, 0, 0, 0, 0, 0, // length
			255, 255, 255, 255, 255, 255, 255, 255, // alloc present
		}`: []byte{
			0, 0, 0, 0, 0, 0, 0, 0,
			255, 255, 255, 255, 255, 255, 255, 255,
		},
		`
		{
			0x0, 0xff, 0xA, 0x0a, 7,
		}`: []byte{
			0, 255, 10, 10, 7,
		},
		`
		{
			'h', 'e', 'l', 'l', 'o',
		}`: []byte{
			'h', 'e', 'l', 'l', 'o',
		},
	})
}

func TestParseSuccessCase(t *testing.T) {
	parsingToCheck{
		t: t,
		fn: func(p *Parser) (interface{}, error) {
			var all ir.All
			if err := p.parseSection(&all); err != nil {
				return nil, err
			} else if len(all.Success) != 1 {
				return nil, fmt.Errorf("did not parse success section")
			}
			return all.Success[0], nil
		},
	}.checkSuccess(map[string]interface{}{
		`
		success("OneStringOfMaxLengthFive-empty") {
			value = OneStringOfMaxLengthFive {
				first: "four",
			}
			bytes = {
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			}
		}`: ir.Success{
			Name: "OneStringOfMaxLengthFive-empty",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: map[string]ir.Value{
					"first": "four",
				},
			},
			Bytes: []byte{
				0, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
			},
		},
	})
}

func TestParseFailsToEncodeCase(t *testing.T) {
	parsingToCheck{
		t: t,
		fn: func(p *Parser) (interface{}, error) {
			var all ir.All
			if err := p.parseSection(&all); err != nil {
				return nil, err
			} else if len(all.FailsToEncode) != 1 {
				return nil, fmt.Errorf("did not parse fails_to_encode section")
			}
			return all.FailsToEncode[0], nil
		},
	}.checkSuccess(map[string]interface{}{
		`
		fails_to_encode("OneStringOfMaxLengthFive-too-long") {
			value = OneStringOfMaxLengthFive {
				the_string: "bonjour", // 6 characters
			}
			err = FIDL_STRING_TOO_LONG
		}`: ir.FailsToEncode{
			Name: "OneStringOfMaxLengthFive-too-long",
			Value: ir.Object{
				Name: "OneStringOfMaxLengthFive",
				Fields: map[string]ir.Value{
					"the_string": "bonjour",
				},
			},
			Err: "FIDL_STRING_TOO_LONG",
		},
	})
}

func TestParseFailsToDecodeCase(t *testing.T) {
	parsingToCheck{
		t: t,
		fn: func(p *Parser) (interface{}, error) {
			var all ir.All
			if err := p.parseSection(&all); err != nil {
				return nil, err
			} else if len(all.FailsToDecode) != 1 {
				return nil, fmt.Errorf("did not parse fails_to_decode section")
			}
			return all.FailsToDecode[0], nil
		},
	}.checkSuccess(map[string]interface{}{
		`
		fails_to_decode("OneStringOfMaxLengthFive-wrong-length") {
			bytes = {
				1, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				// one character missing
			}
			err = FIDL_STRING_TOO_LONG
		}`: ir.FailsToDecode{
			Name: "OneStringOfMaxLengthFive-wrong-length",
			Bytes: []byte{
				1, 0, 0, 0, 0, 0, 0, 0, // length
				255, 255, 255, 255, 255, 255, 255, 255, // alloc present
				// one character missing
			},
			Err: "FIDL_STRING_TOO_LONG",
		},
	})
}

type parsingToCheck struct {
	t  *testing.T
	fn func(*Parser) (interface{}, error)
}

func (c parsingToCheck) checkSuccess(cases map[string]interface{}) {
	for input, expected := range cases {
		c.t.Run(input, func(t *testing.T) {
			p := NewParser("", strings.NewReader(input))
			actual, err := c.fn(p)
			if err != nil {
				t.Fatal(err)
			}

			t.Logf("expected: %T %v", expected, expected)
			t.Logf("actual: %T %v", actual, actual)
			if diff := cmp.Diff(expected, actual); diff != "" {
				t.Fatalf("expected != actual (-want +got)\n%s", diff)
			}
		})
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
