// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
Contains the code to sort elements by name according to somewhat specific rules.
See elementSlice below for details.
*/

package summarize

import (
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// infty is a maximal string.
const infty = "\xff"

// inftyStr maps "" to a maximal string, used to sort the Elements in a specific way.
func inftyStr(s fidlgen.Identifier) fidlgen.Identifier {
	if s == "" {
		return infty
	}
	return s
}

// elementSlice is used to sort a slice of Elements.
//
// The identifiers are sorted as follows: compare libraries first; if
// equal, compare declarations; if equal compare members. Exceptions: a
// library always compares last.
type elementSlice []Element

var _ sort.Interface = (*elementSlice)(nil)

func (e elementSlice) Len() int {
	return len(e)
}

func (e elementSlice) Less(i, j int) bool {
	ni := newFqn(e[i].Name())
	nj := newFqn(e[j].Name())
	return ni.Less(nj)
}

func (e elementSlice) Swap(i, j int) {
	e[j], e[i] = e[i], e[j]
}

// fqn is a fully qualified name for a declaration. "library/decl.name".
// The fully qualified names are sorted as follows: compare libraries first; if
// equal, compare declarations; if equal compare members. Exceptions: a library
// always compares last.
type fqn fidlgen.CompoundIdentifier

// newFqn parses out a fqn from the given string.
func newFqn(name Name) fqn {
	ci := fidlgen.EncodedCompoundIdentifier(name).Parse()
	// Convert empty strings to a "maximal" marker to get the expected sort ordering.
	// If 'name' is a library, it will end up in ci.Name. Otherwise it will end
	// up parsed in ci.Library.
	ci.Name = inftyStr(ci.Name)
	ci.Member = inftyStr(ci.Member)
	return fqn(ci)
}

func (f fqn) isLibrary() bool {
	// This is apparently how compound identifiers encode libraries.
	return len(f.Library) == 1 && f.Library[0] == ""
}

func (f fqn) name() string {
	if f.isLibrary() {
		return infty
	}
	return string(f.Name)
}

func (f fqn) library() string {
	if f.isLibrary() {
		// If f.Library is empty, then this fqn contains a library identifier.
		return string(f.Name)
	}
	// Couldn't find a way to recover the dotted notation from library name.
	var ret []string
	for _, c := range f.Library {
		ret = append(ret, string(c))
	}
	return strings.Join(ret, ".")
}

func (this fqn) Less(that fqn) bool {
	// Libraries compare regularly.
	if this.isLibrary() && that.isLibrary() {
		return this.Name < that.Name
	}
	// Special: library is always last if compared to a declaration.
	if this.isLibrary() {
		return false
	}
	if that.isLibrary() {
		return true
	}
	// The rest of the comparison is as expected.
	thisLib := this.library()
	thatLib := that.library()
	if thisLib == thatLib {
		if this.name() == that.name() {
			return this.Member < that.Member
		}
		return this.name() < that.name()
	}
	return thisLib < thatLib
}
