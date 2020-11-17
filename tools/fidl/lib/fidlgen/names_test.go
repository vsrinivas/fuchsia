// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestNameParts(t *testing.T) {
	type testCase struct {
		input  string
		output []string
	}
	tests := []testCase{
		{
			input:  "",
			output: []string{""},
		},
		{
			input:  "snake_case_string",
			output: []string{"snake", "case", "string"},
		},
		{
			input:  "lowerCamelCaseString",
			output: []string{"lower", "Camel", "Case", "String"},
		},
		{
			input:  "UpperCamelCaseString",
			output: []string{"Upper", "Camel", "Case", "String"},
		},
		{
			input:  "CONST_CASE_STRING",
			output: []string{"CONST", "CASE", "STRING"},
		},
		{
			input:  "friendly case string",
			output: []string{"friendly case string"},
		},
		{
			input:  "stringThatHas02Digits",
			output: []string{"string", "That", "Has02", "Digits"},
		},
		{
			input:  "mixedLowerCamel_snakeCaseString",
			output: []string{"mixed", "Lower", "Camel", "snake", "Case", "String"},
		},
		{
			input:  "MixedUpperCamel_SnakeCaseString",
			output: []string{"Mixed", "Upper", "Camel", "Snake", "Case", "String"},
		},
		{
			input:  "multiple__underscores",
			output: []string{"multiple", "", "underscores"},
		},
	}
	for _, test := range tests {
		output := nameParts(test.input)
		if diff := cmp.Diff(output, test.output); len(diff) > 0 {
			t.Errorf("input %q produced unexpected output: %s", test.input, diff)
		}
	}
}

func TestToSnakeCase(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "",
			output: "",
		},
		{
			input:  "snake_case_string",
			output: "snake_case_string",
		},
		{
			input:  "lowerCamelCaseString",
			output: "lower_camel_case_string",
		},
		{
			input:  "UpperCamelCaseString",
			output: "upper_camel_case_string",
		},
		{
			input:  "CONST_CASE_STRING",
			output: "const_case_string",
		},
		{
			input:  "friendly case string",
			output: "friendly case string",
		},
		{
			input:  "stringThatHas02Digits",
			output: "string_that_has02_digits",
		},
		{
			input:  "mixedLowerCamel_snakeCaseString",
			output: "mixed_lower_camel_snake_case_string",
		},
		{
			input:  "MixedUpperCamel_SnakeCaseString",
			output: "mixed_upper_camel_snake_case_string",
		},
		{
			input:  "multiple__underscores",
			output: "multiple__underscores",
		},
	}
	for _, test := range tests {
		output := ToSnakeCase(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestToUpperCamelCase(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "",
			output: "_",
		},
		{
			input:  "snake_case_string",
			output: "SnakeCaseString",
		},
		{
			input:  "lowerCamelCaseString",
			output: "LowerCamelCaseString",
		},
		{
			input:  "UpperCamelCaseString",
			output: "UpperCamelCaseString",
		},
		{
			input:  "CONST_CASE_STRING",
			output: "ConstCaseString",
		},
		{
			input:  "friendly case string",
			output: "Friendly Case String",
		},
		{
			input:  "stringThatHas02Digits",
			output: "StringThatHas02Digits",
		},
		{
			input:  "mixedLowerCamel_snakeCaseString",
			output: "MixedLowerCamelSnakeCaseString",
		},
		{
			input:  "MixedUpperCamel_SnakeCaseString",
			output: "MixedUpperCamelSnakeCaseString",
		},
		{
			input:  "multiple__underscores",
			output: "Multiple_Underscores",
		},
	}
	for _, test := range tests {
		output := ToUpperCamelCase(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestToLowerCamelCase(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "",
			output: "_",
		},
		{
			input:  "snake_case_string",
			output: "snakeCaseString",
		},
		{
			input:  "lowerCamelCaseString",
			output: "lowerCamelCaseString",
		},
		{
			input:  "UpperCamelCaseString",
			output: "upperCamelCaseString",
		},
		{
			input:  "CONST_CASE_STRING",
			output: "constCaseString",
		},
		{
			input:  "friendly case string",
			output: "friendly case string",
		},
		{
			input:  "stringThatHas02Digits",
			output: "stringThatHas02Digits",
		},
		{
			input:  "mixedLowerCamel_snakeCaseString",
			output: "mixedLowerCamelSnakeCaseString",
		},
		{
			input:  "MixedUpperCamel_SnakeCaseString",
			output: "mixedUpperCamelSnakeCaseString",
		},
		{
			input:  "multiple__underscores",
			output: "multiple_Underscores",
		},
	}
	for _, test := range tests {
		output := ToLowerCamelCase(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestToFriendlyCase(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "",
			output: "",
		},
		{
			input:  "snake_case_string",
			output: "snake case string",
		},
		{
			input:  "lowerCamelCaseString",
			output: "lower camel case string",
		},
		{
			input:  "UpperCamelCaseString",
			output: "upper camel case string",
		},
		{
			input:  "CONST_CASE_STRING",
			output: "const case string",
		},
		{
			input:  "friendly case string",
			output: "friendly case string",
		},
		{
			input:  "stringThatHas02Digits",
			output: "string that has02 digits",
		},
		{
			input:  "mixedLowerCamel_snakeCaseString",
			output: "mixed lower camel snake case string",
		},
		{
			input:  "MixedUpperCamel_SnakeCaseString",
			output: "mixed upper camel snake case string",
		},
		{
			input:  "multiple__underscores",
			output: "multiple  underscores",
		},
	}
	for _, test := range tests {
		output := ToFriendlyCase(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestConstNameToAllCapsSnake(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "k",
			output: "K",
		},
		{
			input:  "kA",
			output: "A",
		},
		{
			input:  "kCamelCase",
			output: "CAMEL_CASE",
		},
		{
			input:  "kA1B",
			output: "A1_B",
		},
		{
			input:  "stringNotBeginningWithK",
			output: "STRING_NOT_BEGINNING_WITH_K",
		},
	}
	for _, test := range tests {
		output := ConstNameToAllCapsSnake(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestRemoveLeadingK(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  "k",
			output: "k",
		},
		{
			input:  "kA",
			output: "A",
		},
		{
			input:  "kCamelCase",
			output: "CamelCase",
		},
		{
			input:  "Abc",
			output: "Abc",
		},
	}
	for _, test := range tests {
		output := RemoveLeadingK(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}
