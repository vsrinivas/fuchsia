// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestReadNameAndAssociatedHelpers(t *testing.T) {
	cases := []struct {
		input            string
		declName         string
		libraryName      string
		libraryNameParts []string
	}{
		{
			input:            "zx/rights",
			declName:         "rights",
			libraryName:      "zx",
			libraryNameParts: []string{"zx"},
		},
		{
			input:            "fuchsia.mem/Buffer",
			declName:         "Buffer",
			libraryName:      "fuchsia.mem",
			libraryNameParts: []string{"fuchsia", "mem"},
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

		if name.LibraryName() != ex.libraryName {
			t.Errorf("library name: expected %s, was %s", ex.libraryName, name.LibraryName())
		}

		if diff := cmp.Diff(name.LibraryNameParts(), ex.libraryNameParts); len(diff) > 0 {
			t.Errorf("library name parts: expected %v, was %v", ex.libraryNameParts, name.LibraryNameParts())
		}
	}
}
