// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestReadLibraryNameAndAssociatedHelpers(t *testing.T) {
	cases := []struct {
		input            string
		libraryName      string
		libraryNameParts []string
	}{
		{
			input:            "zx",
			libraryName:      "zx",
			libraryNameParts: []string{"zx"},
		},
		{
			input:            "fuchsia.mem",
			libraryName:      "fuchsia.mem",
			libraryNameParts: []string{"fuchsia", "mem"},
		},
		{
			input:            "fuchsia.ui.scenic",
			libraryName:      "fuchsia.ui.scenic",
			libraryNameParts: []string{"fuchsia", "ui", "scenic"},
		},
	}
	for _, ex := range cases {
		name, err := ReadLibraryName(ex.input)
		if err != nil {
			t.Fatal(err)
		}

		if name.FullyQualifiedName() != ex.input {
			t.Errorf("fully qualified name: expected %s, was %s", ex.input, name.FullyQualifiedName())
		}

		if diff := cmp.Diff(name.Parts(), ex.libraryNameParts); len(diff) > 0 {
			t.Errorf("library name parts: expected %v, was %v", ex.libraryNameParts, name.Parts())
		}
	}
}

func TestReadBadLibraryNames(t *testing.T) {
	cases := []string{
		"",
		"no_underscores",
		"no spaces",
		"No.Capital.Letters",
		"no.trailing.dot.",
		".no.leading.dot",
	}
	for _, input := range cases {
		if _, err := ReadLibraryName(input); err == nil {
			t.Errorf("invalid library name '%s' did not trigger an error", input)
		}
	}
}

func TestReadNameAndAssociatedHelpers(t *testing.T) {
	cases := []struct {
		input            string
		declName         string
		libraryName      string
		libraryNameParts []string
	}{
		{
			input:       "zx/rights",
			declName:    "rights",
			libraryName: "zx",
		},
		{
			input:       "fuchsia.mem/Buffer",
			declName:    "Buffer",
			libraryName: "fuchsia.mem",
		},
	}
	for _, ex := range cases {
		name, err := ReadName(ex.input)
		if err != nil {
			t.Fatal(err)
		}

		if name.FullyQualifiedName() != ex.input {
			t.Errorf("fully qualified name: expected %s, was %s", ex.input, name.FullyQualifiedName())
		}

		if name.DeclarationName() != ex.declName {
			t.Errorf("declaration name: expected %s, was %s", ex.declName, name.DeclarationName())
		}

		if actualLibraryName := name.LibraryName().FullyQualifiedName(); actualLibraryName != ex.libraryName {
			t.Errorf("library name: expected %s, was %s", ex.libraryName, actualLibraryName)
		}
	}
}

func TestNamesCanBeUsedAsKeyTypes(t *testing.T) {
	// Keys in maps must be comparable. By defining the maps below, we ensure
	// that they follow the requirement as this will be checked by the compiler.
	//
	// For instance, while it would be more natural to store the library name
	// parts in a `LibraryName`, slices are not comparable, and we would
	// therefore break this property.
	//
	// see https://golang.org/ref/spec#Comparison_operators
	var (
		byLibraryName     = make(map[LibraryName]struct{})
		byDeclarationName = make(map[Name]struct{})
	)

	byLibraryName[MustReadLibraryName("fuchsia.ui.gfx")] = struct{}{}
	if _, ok := byLibraryName[MustReadLibraryName("fuchsia.ui.gfx")]; !ok {
		t.Errorf("incorrect lookup by library name")
	}

	byDeclarationName[MustReadName("fuchsia.ui/Something")] = struct{}{}
	if _, ok := byDeclarationName[MustReadName("fuchsia.ui/Something")]; !ok {
		t.Errorf("incorrect lookup by declaration name")
	}
}
