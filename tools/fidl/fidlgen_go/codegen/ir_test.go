// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"math"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func TestStackOfBoundsTag(t *testing.T) {
	cases := []struct {
		input    []int
		expected string
	}{
		{
			input:    []int{},
			expected: "",
		},
		{
			input:    []int{1, 2, 3},
			expected: "3,2,1",
		},
		{
			input:    []int{math.MaxInt32, 1, math.MaxInt32},
			expected: ",1,",
		},
	}
	for _, ex := range cases {
		actual := StackOfBoundsTag{ex.input}.String()
		if actual != ex.expected {
			t.Errorf("%v: expected '%s', actual '%s'", ex.input, ex.expected, actual)
		}
	}
}

func TestBindingsPackageDependency(t *testing.T) {
	cases := []struct {
		fidl                 string
		needsBindingsPackage bool
	}{
		{
			fidl:                 "type MyUnion = flexible union { 1: foo uint8; };",
			needsBindingsPackage: true,
		},
		{
			fidl:                 "type MyUnion = strict union { 1: foo uint8; };",
			needsBindingsPackage: false,
		},
		{
			fidl:                 "type MyStruct = struct {};",
			needsBindingsPackage: true,
		},
		{
			fidl:                 "type MyTable = table {};",
			needsBindingsPackage: true,
		},
		{
			fidl:                 "type MyEnum = enum { FOO = 1; BAR = 2; };",
			needsBindingsPackage: true,
		},
		{
			fidl:                 "type MyBits = bits { FOO = 0b01; BAR = 0b10; };",
			needsBindingsPackage: true,
		},
	}
	for _, ex := range cases {
		root := Compile(fidlgentest.EndToEndTest{T: t}.Single("library example; " + ex.fidl))

		hasBindingsPackage := false
		for _, lib := range root.Libraries {
			if lib.Path == BindingsPackage {
				hasBindingsPackage = true
			}
		}

		if hasBindingsPackage != ex.needsBindingsPackage {
			t.Errorf("%s: expected %t, found %t", ex.fidl, ex.needsBindingsPackage, hasBindingsPackage)
		}
	}
}
