// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func TestDerivesToString(t *testing.T) {
	cases := []struct {
		input    derives
		expected string
	}{
		{0, ""},
		{derivesDebug, "#[derive(Debug)]"},
		{derivesPartialOrd, "#[derive(PartialOrd)]"},
		{derivesHash | derivesAsBytes, "#[derive(Hash, zerocopy::AsBytes)]"},
	}
	for _, ex := range cases {
		actual := ex.input.String()
		if actual != ex.expected {
			t.Errorf("%d: expected '%s', actual '%s'", ex.input, ex.expected, actual)
		}
	}
}

func TestDerivesCalculation(t *testing.T) {
	cases := []struct {
		fidl     string
		expected string
	}{
		{
			fidl:     `type MyStruct = struct { field string; };`,
			expected: "#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]",
		},
		{
			fidl:     `type MyStruct = struct { field float32; };`,
			expected: "#[derive(Debug, Copy, Clone, PartialEq, PartialOrd)]",
		},
		{
			fidl:     `type MyStruct = resource struct { field uint64; };`,
			expected: "#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash, zerocopy::AsBytes, zerocopy::FromBytes)]",
		},
		{
			fidl:     `type MyStruct = resource struct {};`,
			expected: "#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]",
		},
	}
	for _, ex := range cases {
		root := Compile(fidlgentest.EndToEndTest{T: t}.Single(`library example; ` + ex.fidl))
		actual := root.Structs[0].Derives.String()
		if ex.expected != actual {
			t.Errorf("%s: expected %s, found %s", ex.fidl, ex.expected, actual)
		}
	}
}
