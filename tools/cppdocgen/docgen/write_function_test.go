// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"bytes"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"testing"
)

func TestWriteFunctionDeclaration(t *testing.T) {
	simpleFn := clangdoc.FunctionInfo{
		Name:       "MyFunction",
		ReturnType: clangdoc.ReturnType{Type: clangdoc.Type{Name: "void"}},
	}

	// Simple version with no adornment.
	out := bytes.Buffer{}
	writeFunctionDeclaration(&simpleFn, "", true, "", &out)
	simpleExpected := "<span class=\"typ\">void</span> <b>MyFunction</b>();\n"
	if out.String() != simpleExpected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), simpleExpected)
	}

	// Version with link and prefix.
	out = bytes.Buffer{}
	writeFunctionDeclaration(&simpleFn, "SomeClass::", true, "foo.md#MyFunction", &out)
	linkExpected := "<span class=\"typ\">void</span> <a href=\"foo.md#MyFunction\">SomeClass::<b>MyFunction</b></a>();\n"
	if out.String() != linkExpected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), linkExpected)
	}

	// Declare a function with some more complex parameters.
	paramFn := clangdoc.FunctionInfo{
		Name: "MyFunction",
		// Returns a std::__2::string which is the internal name for std::string.
		ReturnType: clangdoc.ReturnType{Type: clangdoc.Type{Name: "string", Path: "std/__2"}},
		Params: []clangdoc.FieldTypeInfo{
			{Name: "", Type: clangdoc.Type{Name: "int"}}, // Un-named.
			{Name: "b", Type: clangdoc.Type{Name: "vector<char>&", Path: "std/__2"}},
		},
	}

	out = bytes.Buffer{}
	writeFunctionDeclaration(&paramFn, "", true, "", &out)
	paramExpected := "<span class=\"typ\">std::string</span> <b>MyFunction</b>(<span class=\"typ\">int</span>,\n" +
		"                       <span class=\"typ\">std::vector&lt;char&gt;&amp;</span> b);\n"
	if out.String() != paramExpected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), paramExpected)
	}
}
