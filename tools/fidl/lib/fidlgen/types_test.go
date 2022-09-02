// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"encoding/json"
	"math"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
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

	var method Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
	}
}

func TestEncodedCompoundIdentifierParsing(t *testing.T) {
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
		output := test.input.Parse()
		diff := cmp.Diff(output, test.expectedOutput)
		if len(diff) > 0 {
			t.Errorf("unexpected output for input %q diff: %s", test.input, diff)
		}
	}
}

func TestExperimentsParsing(t *testing.T) {
	type testCase struct {
		desc     string
		ir       string
		expected Experiments
	}
	tests := []testCase{
		{
			desc:     "empty",
			ir:       `{"experiments": []}`,
			expected: Experiments{},
		},
		{
			desc:     "single",
			ir:       `{"experiments": ["foo"]}`,
			expected: Experiments{"foo"},
		},
		{
			desc:     "multiple",
			ir:       `{"experiments": ["foo", "bar"]}`,
			expected: Experiments{"foo", "bar"},
		},
	}
	for _, test := range tests {
		ir, err := ReadJSONIrContent([]byte(test.ir))
		if err != nil {
			t.Errorf("case (%s) could not parse JSON IR: %v", test.desc, err)
		}

		actual := ir.Experiments
		opt := cmpopts.SortSlices(func(a, b string) bool { return a < b })
		if diff := cmp.Diff(actual, test.expected, opt); len(diff) > 0 {
			t.Errorf("case (%s) \nexpected: %#v\nactual: %#v\n", test.desc, test.expected, actual)
		}

		// Make sure the "Contains" method works.
		for _, ex := range test.expected {
			if !actual.Contains(ex) {
				t.Errorf("case (%s) Contains(\"%s\") returned false for present experiment", test.desc, ex)
			}
		}
		if actual.Contains(Experiment("should always fail")) {
			t.Errorf("case (%s) Contains always returns true", test.desc)
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
