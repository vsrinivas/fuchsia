// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"reflect"
	"testing"

	"fuchsia.googlesource.com/tools/testsharder"
)

func TestGroupTests(t *testing.T) {
	tests := []struct {
		name   string
		input  []testsharder.Test
		output map[string][]testsharder.Test
	}{{
		name: "should sort tests by name and partition them into subgroups",
		input: []testsharder.Test{
			{Name: "a", OS: "A"},
			{Name: "c", OS: "C"},
			{Name: "e", OS: "B"},
			{Name: "d", OS: "B"},
			{Name: "b", OS: "A"},
		},
		output: map[string][]testsharder.Test{
			// Note that tests in each subgroup are sorted by name.
			"A": []testsharder.Test{
				{Name: "a", OS: "A"},
				{Name: "b", OS: "A"},
			},
			"B": []testsharder.Test{
				{Name: "d", OS: "B"},
				{Name: "e", OS: "B"},
			},
			"C": []testsharder.Test{
				{Name: "c", OS: "C"},
			},
		},
	}, {
		name:   "should produce an empty map when given empty input",
		input:  []testsharder.Test{},
		output: map[string][]testsharder.Test{},
	}}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			output := groupTests(tt.input, func(test testsharder.Test) string {
				return test.OS
			})

			if !reflect.DeepEqual(tt.output, output) {
				t.Fatalf("got %v, want: '%v'", output, tt.output)
			}
		})
	}
}
