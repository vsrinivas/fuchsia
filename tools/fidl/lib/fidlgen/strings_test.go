// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"testing"
)

func TestSingleQuote(t *testing.T) {
	type testCase struct {
		input  string
		output string
	}
	tests := []testCase{
		{
			input:  ``,
			output: `''`,
		},
		{
			input:  `"`,
			output: `'"'`,
		},
		{
			input:  `abc`,
			output: `'abc'`,
		},
		{
			input:  `\`,
			output: `'\\'`,
		},
		{
			input:  `'`,
			output: `'\''`,
		},
		{
			input:  `\'`,
			output: `'\\\''`,
		},
		{
			input:  `'a"b"c'`,
			output: `'\'a"b"c\''`,
		},
	}
	for _, test := range tests {
		output := SingleQuote(test.input)
		if output != test.output {
			t.Errorf("input %q produced unexpected output. got %q, want %q", test.input, output, test.output)
		}
	}
}

func TestPrintableASCIIRune(t *testing.T) {
	// positive cases
	printableRunes := []rune{
		'h',
		'e',
		'l',
		'0',
		rune(0x20),
		rune(0x7e),
	}
	for _, r := range printableRunes {
		if !PrintableASCIIRune(r) {
			t.Errorf("expected %x to be a printable rune", r)
		}
	}

	// negative cases
	nonPrintableRunes := []rune{
		rune(0x00),
		rune(0x19),
		rune(0x80),
		rune(0x4242),
	}
	for _, r := range nonPrintableRunes {
		if PrintableASCIIRune(r) {
			t.Errorf("did not expect %x to be a printable rune", r)
		}
	}
}

func TestPrintableASCII(t *testing.T) {
	// positive cases
	printableStrings := []string{
		"ahb",
		"aeb",
		"alb",
		"a0b",
		"a\x20b",
		"a\x7eb",
	}
	for _, s := range printableStrings {
		if !PrintableASCII(s) {
			t.Errorf("expected %s to be a printable syring", s)
		}
	}

	// negative cases
	nonPrintableStrings := []string{
		"a\x00b",
		"a\x19b",
		"a\x81b",
	}
	for _, s := range nonPrintableStrings {
		if PrintableASCII(s) {
			t.Errorf("did not expect %s to be a printable string", s)
		}
	}
}
