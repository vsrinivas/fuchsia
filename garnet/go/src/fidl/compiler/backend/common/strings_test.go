package common

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
