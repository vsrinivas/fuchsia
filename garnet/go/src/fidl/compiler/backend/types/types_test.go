// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package types

import (
	"encoding/json"
	"math"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestCanUnmarshalLargeOrdinal(t *testing.T) {
	input := `{
		"ordinal": 18446744073709551615,
		"generated_ordinal": 18446744073709551615
	}`

	var method Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
	}
	if method.GenOrdinal != math.MaxUint64 {
		t.Fatalf("method.GenOrdinal: expected math.MaxUint64, found %d", method.GenOrdinal)
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
