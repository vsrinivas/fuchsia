// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_test

import (
	"encoding/json"
	"fmt"
	"math"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

// toDocComment formats doc comments in a by adding a leading space and a
// trailing newline.
func toDocComment(input string) string {
	return " " + input + "\n"
}

func TestCanUnmarshalLargeOrdinal(t *testing.T) {
	input := `{
		"ordinal": 18446744073709551615
	}`

	var method fidlgen.Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
	}
}

func TestCanUnmarshalAttributeValue(t *testing.T) {
	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		@doc("MyUnion")
		@UpperCamelCase
		@lower_snake_case
		@CAPS
		type MyUnion = flexible union {
			/// my_union_member
			@on_the_member
			1: my_union_member bool;
		};
	`)

	// MyUnion
	unionName := string(root.Unions[0].Name)
	wantUnionName := "MyUnion"
	unionAttrs := root.Unions[0].Attributes
	if len(unionAttrs.Attributes) != 4 {
		t.Errorf("got %d attributes on '%s', want %d", len(unionAttrs.Attributes), unionName, 4)
	}

	attr, found := unionAttrs.LookupAttribute("doc")
	if !found {
		t.Errorf("'%s' 'doc' attribute: not found", unionName)
	}
	if arg, found := attr.LookupArgStandalone(); !found || arg.ValueString() != wantUnionName {
		t.Errorf("'%s' 'doc' attribute: got '%s', want '%s'", unionName, arg.ValueString(), wantUnionName)
	}
	if _, found := unionAttrs.LookupAttribute("UpperCamelCase"); !found {
		t.Errorf("'%s' 'UpperCamelCase' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("upper_camel_case"); !found {
		t.Errorf("'%s' 'UpperCamelCase' attribute: not found (using lower-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("LowerSnakeCase"); !found {
		t.Errorf("'%s' 'lower_snake_case' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("lower_snake_case"); !found {
		t.Errorf("'%s' 'lower_snake_case' attribute: not found (using lower-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("Caps"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("caps"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using lower-snake-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("CAPS"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using default string for search)", unionName)
	}

	// my_union_member
	unionMemName := string(root.Unions[0].Members[0].Name)
	wantUnionMemName := "my_union_member"
	unionMemAttrs := root.Unions[0].Members[0].Attributes
	if unionMemName != wantUnionMemName {
		t.Errorf("got union with name '%s', want '%s'", unionMemName, wantUnionMemName)
	}
	if len(unionMemAttrs.Attributes) != 2 {
		t.Errorf("got %d attributes on '%s', want %d", len(unionMemAttrs.Attributes), unionMemName, 2)
	}

	attr, found = unionMemAttrs.LookupAttribute("doc")
	if !found {
		t.Errorf("'%s' 'doc' attribute: not found", unionMemName)
	}
	if arg, found := attr.LookupArgStandalone(); !found || arg.ValueString() != toDocComment(wantUnionMemName) {
		t.Errorf("'%s' 'doc' attribute: got '%s', want '%s'", unionMemName, arg.ValueString(), toDocComment(wantUnionMemName))
	}
	if _, found := unionMemAttrs.LookupAttribute("on_the_member"); !found {
		t.Errorf("'%s' 'on_the_member' attribute: not found", unionMemName)
	}
	if _, found := unionMemAttrs.LookupAttribute("Missing"); found {
		t.Errorf("'%s' 'missing' attribute: found when non-existant", unionMemName)
	}
	if _, found := unionMemAttrs.LookupAttribute("missing"); found {
		t.Errorf("'%s' 'missing' attribute: found when non-existant", unionMemName)
	}
}

func TestCanUnmarshalSignedEnumUnknownValue(t *testing.T) {
	inputTmpl := `{
		"enum_declarations": [
			{
			"type": "int32",
			"strict": false,
			"maybe_unknown_value": %s
			}
		]
	}`

	cases := []struct {
		jsonValue     string
		expectedValue int64
	}{
		{"0", 0},
		{"300", 300},
		{"-300", -300},
		{"9223372036854775806", math.MaxInt64 - 1},
		{"9223372036854775807", math.MaxInt64},
		{"-9223372036854775808", math.MinInt64},
	}
	for _, ex := range cases {
		root, err := fidlgen.ReadJSONIrContent([]byte(fmt.Sprintf(inputTmpl, ex.jsonValue)))
		if err != nil {
			t.Fatalf("failed to read JSON IR: %s", err)
		}
		enumOfSignedInt := root.Enums[0]
		unknownValue, err := enumOfSignedInt.UnknownValueAsInt64()
		if err != nil {
			t.Fatalf("failed to retrieve UnknownValueAsInt64: %s", err)
		}
		if unknownValue != ex.expectedValue {
			t.Fatalf("jsonValue '%s': expected %d, actual %d",
				ex.jsonValue, ex.expectedValue, unknownValue)
		}
	}
}

func TestCanUnmarshalUnsignedEnumUnknownValue(t *testing.T) {
	inputTmpl := `{
		"enum_declarations": [
			{
			"type": "uint32",
			"strict": false,
			"maybe_unknown_value": %s
			}
		]
	}`

	cases := []struct {
		jsonValue     string
		expectedValue uint64
	}{
		{"0", 0},
		{"300", 300},
		{"18446744073709551614", math.MaxUint64 - 1},
		{"18446744073709551615", math.MaxUint64},
	}
	for _, ex := range cases {
		root, err := fidlgen.ReadJSONIrContent([]byte(fmt.Sprintf(inputTmpl, ex.jsonValue)))
		if err != nil {
			t.Fatalf("failed to read JSON IR: %s", err)
		}
		enumOfSignedInt := root.Enums[0]
		unknownValue, err := enumOfSignedInt.UnknownValueAsUint64()
		if err != nil {
			t.Fatalf("failed to retrieve UnknownValueAsUint64: %s", err)
		}
		if unknownValue != ex.expectedValue {
			t.Fatalf("jsonValue '%s': expected %d, actual %d",
				ex.jsonValue, ex.expectedValue, unknownValue)
		}
	}
}

func TestCanUnmarshalBitsStrictness(t *testing.T) {
	inputTmpl := `{
		"bits_declarations": [
			{
				"type": {
					"kind": "primitive",
					"subtype": "uint32",
					"type_shape_v1": {},
					"type_shape_v2": {}
				},
				"mask": "1",
				"members": [],
				"strict": %s
			}
		]
	}`

	cases := []struct {
		jsonValue     string
		expectedValue fidlgen.Strictness
	}{
		{"false", fidlgen.IsFlexible},
		{"true", fidlgen.IsStrict},
	}
	for _, ex := range cases {
		root, err := fidlgen.ReadJSONIrContent([]byte(fmt.Sprintf(inputTmpl, ex.jsonValue)))
		if err != nil {
			t.Fatalf("failed to read JSON IR: %s", err)
		}
		bits := root.Bits[0]
		if bits.Strictness != ex.expectedValue {
			t.Fatalf("jsonValue '%s': expected %v, actual %v",
				ex.jsonValue, ex.expectedValue, bits.Strictness)
		}
	}
}

func TestParseCompoundIdentifier(t *testing.T) {
	type testCase struct {
		input          fidlgen.EncodedCompoundIdentifier
		expectedOutput fidlgen.CompoundIdentifier
	}
	tests := []testCase{
		{
			input:          "Decl",
			expectedOutput: compoundIdentifier([]string{""}, "Decl", ""),
		},
		{
			input:          "fuchsia.some.library/Decl",
			expectedOutput: compoundIdentifier([]string{"fuchsia", "some", "library"}, "Decl", ""),
		},
		{
			input:          "Name.MEMBER",
			expectedOutput: compoundIdentifier([]string{""}, "Name", "MEMBER"),
		},

		{
			input:          "fuchsia.some.library/Decl.MEMBER",
			expectedOutput: compoundIdentifier([]string{"fuchsia", "some", "library"}, "Decl", "MEMBER"),
		},
	}
	for _, test := range tests {
		output := fidlgen.ParseCompoundIdentifier(test.input)
		diff := cmp.Diff(output, test.expectedOutput)
		if len(diff) > 0 {
			t.Errorf("unexpected output for input %q diff: %s", test.input, diff)
		}
	}
}

func compoundIdentifier(library []string, name, member string) fidlgen.CompoundIdentifier {
	var convertedLibrary fidlgen.LibraryIdentifier
	for _, part := range library {
		convertedLibrary = append(convertedLibrary, fidlgen.Identifier(part))
	}
	return fidlgen.CompoundIdentifier{
		Library: convertedLibrary,
		Name:    fidlgen.Identifier(name),
		Member:  fidlgen.Identifier(member),
	}
}
