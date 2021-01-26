// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Element describes a single platform surface element.
type Element interface {
	fmt.Stringer
}

var _ = []Element{
	(*library)(nil),
}

// library contains a library declaration.
type library fidlgen.Root

// String implements Element.
func (l library) String() string {
	return fmt.Sprintf("library %v", l.Name)
}

// Elements obtains the API elements present in the given root.
func Elements(root fidlgen.Root) []Element {
	var ret []Element
	ret = append(ret, library(root))
	return ret
}

/// Summarize produces an API summary for the FIDL AST from the root.
func Summarize(root fidlgen.Root, out io.Writer) error {
	elems := Elements(root)
	for _, e := range elems {
		fmt.Fprintf(out, "%v\n", e)
	}
	return nil
}
