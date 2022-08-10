// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"io"
)

// Writes either "()" for functions with no arguments, or "(...)" for functions with arguments.
// This is used to make things look like function calls without listing the arguments or incorrectly
// implying that they take no arguments.
func functionEllipsesParens(fn *clangdoc.FunctionInfo) string {
	if len(fn.Params) == 0 {
		return "()"
	}
	return "(…)"
}

// If linkDest is nonempty this will make the function name a link.
func writeFunctionDeclaration(fn *clangdoc.FunctionInfo, namePrefix string, includeReturnType bool, linkDest string, f io.Writer) {
	retTypeLen := 0
	if includeReturnType {
		qualType, n := getEscapedTypeName(fn.ReturnType.Type)
		fmt.Fprintf(f, "<span class=\"typ\">%s</span> ", qualType)
		retTypeLen = n + 1 // Include space after.
	}

	// Name (optionally linked and with optional prefix).
	if len(linkDest) > 0 {
		fmt.Fprintf(f, "<a href=\"%s\">", linkDest)
	}
	fmt.Fprintf(f, "%s<b>%s</b>", namePrefix, fn.Name)
	if len(linkDest) > 0 {
		fmt.Fprintf(f, "</a>")
	}

	fmt.Fprintf(f, "(")

	// Indent is type + space + prefix + name + paren.
	indent := makeIndent(retTypeLen + len(namePrefix) + len(fn.Name) + 1)
	for i, param := range fn.Params {
		if i > 0 {
			fmt.Fprintf(f, ",\n")
			f.Write(indent)
		}

		tn, _ := getEscapedTypeName(param.Type)
		if len(param.Name) == 0 {
			// Unnamed parameter.
			fmt.Fprintf(f, "<span class=\"typ\">%s</span>", tn)
		} else {
			fmt.Fprintf(f, "<span class=\"typ\">%s</span> %s", tn, param.Name)
		}
	}

	fmt.Fprintf(f, ");\n")
}

// Writes the body of a function reference. This is used for both standalone functions and member
// functions.
//
// The |namePrefix| is prepended to the definition for defining class or namespace information.
// This could be extracted from the function but this lets the caller decide which information to
// include.
func writeFunctionBody(fn *clangdoc.FunctionInfo, namePrefix string, includeReturnType bool, f io.Writer) {
	writePreHeader(f)
	writeFunctionDeclaration(fn, namePrefix, includeReturnType, "", f)
	writePreFooter(f)

	writeComment(fn.Description, f)
}

// Writes the reference section for a standalone function.
func writeFunctionSection(fn *clangdoc.FunctionInfo, f io.Writer) {
	fmt.Fprintf(f, "## %s%s {:#%s}\n\n", fn.Name, functionEllipsesParens(fn), functionHtmlId(fn))

	// Include the qualified namespaces as a prefix.
	writeFunctionBody(fn, functionScopePrefix(fn), true, f)
}

// Interface for sorting the function list by function name.
type functionByName []*clangdoc.FunctionInfo

func (f functionByName) Len() int {
	return len(f)
}
func (f functionByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f functionByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

// Use for standalone functions. For member functions use memberFunctionHtmlId()
func functionHtmlId(f *clangdoc.FunctionInfo) string {
	// Just using the name won't technically be unique. There could be different functions with
	// the same name in different namespaces. But since there can be overrides with name
	// collisions anyway, we don't worry about that. Keeping this simple makes it easy to write
	// links by hand if necessary.
	return f.Name
}

func functionLink(f *clangdoc.FunctionInfo) string {
	return HeaderReferenceFile(f.Location[0].Filename) + "#" + functionHtmlId(f)
}

func functionScopePrefix(f *clangdoc.FunctionInfo) string {
	result := ""

	// The order is in reverse of C++.
	for i := len(f.Namespace) - 1; i >= 0; i-- {
		result += f.Namespace[i].Name + "::"
	}
	return result
}

// Returns the fully-qualified name of a function.
func functionFullName(f *clangdoc.FunctionInfo) string {
	return functionScopePrefix(f) + f.Name
}
