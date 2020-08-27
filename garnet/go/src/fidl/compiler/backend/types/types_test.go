// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

import (
	"encoding/json"
	"fmt"
	"math"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestCanUnmarshalLargeOrdinal(t *testing.T) {
	input := `{
		"ordinal": 18446744073709551615
	}`

	var method Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
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
		root, err := ReadJSONIrContent([]byte(fmt.Sprintf(inputTmpl, ex.jsonValue)))
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
		root, err := ReadJSONIrContent([]byte(fmt.Sprintf(inputTmpl, ex.jsonValue)))
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

func TestParseCompoundIdentifier(t *testing.T) {
	type testCase struct {
		input          EncodedCompoundIdentifier
		expectedOutput CompoundIdentifier
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
		output := ParseCompoundIdentifier(test.input)
		diff := cmp.Diff(output, test.expectedOutput)
		if len(diff) > 0 {
			t.Errorf("unexpected output for input %q diff: %s", test.input, diff)
		}
	}
}

func compoundIdentifier(library []string, name, member string) CompoundIdentifier {
	var convertedLibrary LibraryIdentifier
	for _, part := range library {
		convertedLibrary = append(convertedLibrary, Identifier(part))
	}
	return CompoundIdentifier{
		Library: convertedLibrary,
		Name:    Identifier(name),
		Member:  Identifier(member),
	}
}
