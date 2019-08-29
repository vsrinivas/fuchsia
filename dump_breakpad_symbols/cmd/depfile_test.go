// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"reflect"
	"testing"
)

func TestDepfile(t *testing.T) {
	tests := []struct {
		// A name for this test case
		name string

		// The input depfile.
		depfile *depfile

		// The expected contents of the file produced by WriteTo.
		output string

		// Whether to expect an error when creating the dep file.
		expectErr bool
	}{
		{
			name: "should create a dep file with a single input",
			depfile: &depfile{
				outputPath: "a",
				inputPaths: []string{"b"},
			},
			output: "a: b\n",
		},
		{
			name: "should create a dep file with multiple inputs",
			depfile: &depfile{
				outputPath: "a",
				inputPaths: []string{"b", "c"},
			},
			output: "a: b c\n",
		},
		{
			name: "should err if the output path is missing",
			depfile: &depfile{
				inputPaths: []string{"b"},
			},
			expectErr: true,
		},
		{
			name: "should err if the input paths are missing",
			depfile: &depfile{
				outputPath: "a",
			},
			expectErr: true,
		},
		{
			name: "should err if any of the input paths is empty (first)",
			depfile: &depfile{
				outputPath: "a",
				inputPaths: []string{"", "b"},
			},
			expectErr: true,
		},
		{
			name: "should err if any of the input paths is empty (last)",
			depfile: &depfile{
				outputPath: "a",
				inputPaths: []string{"b", ""},
			},
			expectErr: true,
		},
		{
			name: "should err if any of the input paths is empty (nth)",
			depfile: &depfile{
				outputPath: "a",
				inputPaths: []string{"b", "c", "", "e"},
			},
			expectErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var buf bytes.Buffer
			_, err := tt.depfile.WriteTo(&buf)
			if tt.expectErr && err == nil {
				t.Errorf("wanted an error but got nil")
				return
			}
			if !tt.expectErr && err != nil {
				t.Errorf("got an unexpected error: %v", err)
			}
			actual := buf.String()
			expected := tt.output
			if !reflect.DeepEqual(actual, expected) {
				t.Errorf("got:\n\n%v\n\nwanted:\n\n%v\n\n", actual, expected)
			}
		})
	}
}
