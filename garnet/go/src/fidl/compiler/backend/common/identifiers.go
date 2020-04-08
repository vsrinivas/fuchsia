// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"fmt"
	"strings"
)

// Name represents a FIDL declaration name, consisting of a FIDL library, and
// a FIDL declration such as `fuchsia.mem` and `Buffer`.
type Name struct {
	libParts []string
	declName string
}

// LibraryNameParts returns the library name in parts, e.g. `fuchsia`, `mem`.
func (name Name) LibraryNameParts() []string {
	return name.libParts
}

// LibraryName returns the library name, e.g. `fuchsia.mem`.
func (name Name) LibraryName() string {
	return strings.Join(name.libParts, ".")
}

// DeclarationName returns the declaration name, e.g. `Buffer`.
func (name Name) DeclarationName() string {
	return name.declName
}

// FullyQualifiedName returns the fully qualified name, e.g. `fuchsia.mem/Buffer`.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func (name Name) FullyQualifiedName() string {
	return fmt.Sprintf("%s/%s", name.LibraryName(), name.declName)
}

// ReadName reads a name from a fully qualified name.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func ReadName(fullyQualifiedName string) (Name, error) {
	parts := strings.Split(fullyQualifiedName, "/")
	if len(parts) != 2 {
		return Name{}, fmt.Errorf("expected a fully qualified name library.name/DeclarationName, found %s", fullyQualifiedName)
	}
	return Name{
		libParts: strings.Split(parts[0], "."),
		declName: parts[1],
	}, nil
}

// MustReadName reads a name from a fully qualified name, and panics in case of
// error.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func MustReadName(fullyQualifiedName string) Name {
	name, err := ReadName(fullyQualifiedName)
	if err != nil {
		panic(err.Error())
	}
	return name
}
