// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"fmt"
	"regexp"
	"strings"
)

// LibraryName represents a FIDL library name, such as `fuchsia.mem` or
// `fuchsia.ui.scenic`.
type LibraryName struct {
	fqn string
}

// Parts returns the library name in parts, e.g. `fuchsia`, `mem` or
// `fuchsia`, `ui`, `scenic`.
func (name LibraryName) Parts() []string {
	return strings.Split(name.fqn, ".")
}

// FullyQualifiedName returns the fully qualified name, e.g. `fuchsia.mem` or
// `fuchsia.ui.scenic`.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func (name LibraryName) FullyQualifiedName() string {
	return name.fqn
}

// Name represents a FIDL declaration name, consisting of a FIDL library, and
// a FIDL declration such as `fuchsia.mem` and `Buffer`.
type Name struct {
	libraryName LibraryName
	declName    string
}

// LibraryName returns the library name, e.g. `fuchsia.mem`.
func (name Name) LibraryName() LibraryName {
	return name.libraryName
}

// DeclarationName returns the declaration name, e.g. `Buffer`.
func (name Name) DeclarationName() string {
	return name.declName
}

// FullyQualifiedName returns the fully qualified name, e.g. `fuchsia.mem/Buffer`.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func (name Name) FullyQualifiedName() string {
	return fmt.Sprintf("%s/%s", name.libraryName.fqn, name.declName)
}

var checkLibraryName = regexp.MustCompile("^[a-z][a-z0-9]*(\\.[a-z][a-z0-9]*)*$")

// ReadLibraryName reads a library name from a fully qualified name.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func ReadLibraryName(fullyQualifiedName string) (LibraryName, error) {
	if !checkLibraryName.MatchString(fullyQualifiedName) {
		return LibraryName{}, fmt.Errorf("invalid library name: %s", fullyQualifiedName)
	}
	return LibraryName{fullyQualifiedName}, nil
}

// MustReadLibraryName reads a library name from a fully qualified name, and
// panics in case of error.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func MustReadLibraryName(fullyQualifiedName string) LibraryName {
	name, err := ReadLibraryName(fullyQualifiedName)
	if err != nil {
		panic(err)
	}
	return name
}

// ReadName reads a name from a fully qualified name.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func ReadName(fullyQualifiedName string) (Name, error) {
	parts := strings.Split(fullyQualifiedName, "/")
	if len(parts) != 2 {
		return Name{}, fmt.Errorf("expected a fully qualified name library.name/DeclarationName, found %s", fullyQualifiedName)
	}
	libraryName, err := ReadLibraryName(parts[0])
	if err != nil {
		return Name{}, err
	}
	return Name{
		libraryName: libraryName,
		declName:    parts[1],
	}, nil
}

// MustReadName reads a name from a fully qualified name, and panics in case of
// error.
//
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-043#fully_qualified_names
func MustReadName(fullyQualifiedName string) Name {
	name, err := ReadName(fullyQualifiedName)
	if err != nil {
		panic(err)
	}
	return name
}
