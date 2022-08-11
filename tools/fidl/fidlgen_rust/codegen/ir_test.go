// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"strings"
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
		root := Compile(fidlgentest.EndToEndTest{T: t}.Single(`library example; `+ex.fidl), AllowlistMap{})
		actual := root.Structs[0].Derives.String()
		if ex.expected != actual {
			t.Errorf("%s: expected %s, found %s", ex.fidl, ex.expected, actual)
		}
	}
}

func TestAllowlists(t *testing.T) {
	first := AllowlistName("first")
	second := AllowlistName("second")
	alm := AllowlistMap{
		AllowlistName(first): []EncodedLibraryIdentifier{
			EncodedLibraryIdentifier("foo.bar"),
			EncodedLibraryIdentifier("baz.qux"),
		},
		AllowlistName(second): []EncodedLibraryIdentifier{
			EncodedLibraryIdentifier("baz.qux"),
		},
	}

	cases := []struct {
		desc     string
		fidl     string
		expected []AllowlistName
	}{
		{
			desc: "no allowlist matches",
			fidl: "library no.match;",
		},
		{
			desc:     "single allowlist match",
			fidl:     "library foo.bar;",
			expected: []AllowlistName{first},
		},
		{
			desc:     "multiple allowlist matches",
			fidl:     "library baz.qux;",
			expected: []AllowlistName{first, second},
		},
	}
	for _, ex := range cases {
		gen := NewGenerator("", "")
		root := Compile(fidlgentest.EndToEndTest{T: t}.Single(ex.fidl), alm)
		bytes, err := gen.ExecuteTemplate("GenerateSourceFile", root)
		if err != nil {
			t.Fatalf("%s (%s): could not apply template: %s", ex.fidl, ex.desc, err)
		}

		rust := string(bytes)
		all := root.AllowlistedBy.All()
		if len(all) != len(ex.expected) {
			t.Errorf("%s (%s): got %d allowlist_by markers, want %d", ex.fidl, ex.desc, len(all), len(ex.expected))
		}
		for _, an := range ex.expected {
			if !root.AllowlistedBy.Contains(an) {
				t.Errorf("%s (%s): could not find allowlist %s on root", ex.fidl, ex.desc, an)
			}
			if !strings.Contains(rust, fmt.Sprintf("\n// allowlisted_by = %s", an)) {
				t.Errorf("%s (%s): could not find 'allowlisted_by = %s' in output", ex.fidl, ex.desc, an)
			}
		}
	}
}
