package dart

import (
	"testing"
)

func TestSnakeCaseToLowerCamelCase(t *testing.T) {
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
			input:  "ABc",
			output: "abc",
		},
		{
			input:  "aBc",
			output: "abc",
		},
		{
			input:  "字Bc",
			output: "字bc",
		},
		{
			input:  "CONST_CASE_STRING",
			output: "constCaseString",
		},
		{
			input:  "AbC_XyZ",
			output: "abcXyz",
		},
		{
			input:  "AbC_xYz",
			output: "abcXyz",
		},
	}
	for _, test := range tests {
		output := snakeCaseToLowerCamelCase(test.input)
		if output != test.output {
			t.Errorf("got %v, want %v", output, test.output)
		}
	}
}
