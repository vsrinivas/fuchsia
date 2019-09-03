// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package breakpad

import (
	"bytes"
	"reflect"
	"strings"
	"testing"
)

func TestParseSymbolFile(t *testing.T) {
	tests := []struct {
		name      string
		input     string
		output    *SymbolFile
		expectErr bool
	}{
		{
			name: "should parse when the module section has all values",
			input: strings.Join([]string{
				"MODULE os arch buildid modulename",
				"garbage1",
			}, "\n"),
			output: &SymbolFile{
				ModuleSection: &ModuleSection{
					OS:         "os",
					Arch:       "arch",
					BuildID:    "buildid",
					ModuleName: "modulename",
				},
				remainder: "garbage1",
			},
		},
		{
			name: "should fail to parse a file with less than two lines",
			input: strings.Join([]string{
				"MODULE os arch buildid modulename",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse a file with a leading blank line",
			input: strings.Join([]string{
				"",
				"MODULE os arch buildid modulename",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section is missing one value",
			input: strings.Join([]string{
				"MODULE os arch buildid",
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section is missing two values",
			input: strings.Join([]string{
				"MODULE os arch",
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section is missing three values",
			input: strings.Join([]string{
				"MODULE os",
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section is missing all values",
			input: strings.Join([]string{
				"MODULE",
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section is missing",
			input: strings.Join([]string{
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
		{
			name: "should fail to parse when the module section has extra values",
			input: strings.Join([]string{
				"MODULE os arch buildid modulename whatisthis",
				"INFO CODE_ID ACBDEF",
				"FILE 9 /tmp/test/out/source.cpp",
			}, "\n"),
			expectErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			output, err := ParseSymbolFile(strings.NewReader(tt.input))
			if err != nil && !tt.expectErr {
				t.Errorf("unexpected parse error: %v", err)
				return
			} else if err == nil && tt.expectErr {
				t.Errorf("exected error but got nil with output: %v", output)
				return
			}
			if tt.expectErr {
				return
			}
			expected := tt.output
			actual := output
			if !reflect.DeepEqual(expected.ModuleSection, actual.ModuleSection) {
				t.Errorf("wanted module section:\n\n%v\n\ngot:\n\n%v\n\n", expected.ModuleSection, actual.ModuleSection)
			}
			if expected.remainder != actual.remainder {
				t.Errorf("wanted remainder file contents:\n\n%v\n\ngot:\n\n%v\n\n", expected.remainder, actual.remainder)
			}
		})
	}
}

func TestSymbolFileWriteTo(t *testing.T) {
	tests := []struct {
		name   string
		input  SymbolFile
		output string
	}{
		{
			name: "when module section has all fields",
			input: SymbolFile{
				ModuleSection: &ModuleSection{
					OS:         "os",
					Arch:       "arch",
					BuildID:    "buildid",
					ModuleName: "modulename",
				},
				remainder: strings.Join([]string{
					"remainder line 1",
					"remainder line 2",
				}, "\n"),
			},
			output: strings.Join([]string{
				"MODULE os arch buildid modulename",
				"remainder line 1",
				"remainder line 2",
			}, "\n"),
		},
		{
			// This is not a valid module section, but we still want to verify that it
			// writes the exected data.
			name: "when module section is missing some fields",
			input: SymbolFile{
				ModuleSection: &ModuleSection{
					OS:      "os",
					Arch:    "arch",
					BuildID: "buildid",
				},
				remainder: strings.Join([]string{
					"remainder line 1",
				}, "\n"),
			},
			output: strings.Join([]string{
				"MODULE os arch buildid",
				"remainder line 1",
			}, "\n"),
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var output bytes.Buffer
			if _, err := tt.input.WriteTo(&output); err != nil {
				t.Errorf("unexpected error: %v. input: %v", err, tt.input)
			}
			expected := tt.output
			actual := output.String()
			if expected != actual {
				t.Errorf("wanted:\n\n%v\n\ngot:\n\n%v\n\n", expected, actual)
			}
		})
	}
}
