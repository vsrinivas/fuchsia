// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"io"
)

func enumHtmlId(e *clangdoc.EnumInfo) string {
	// TODO include namespace information in this for proper scoping.
	return getScopeQualifier(e.Namespace, true) + e.Name
}

func enumLink(e *clangdoc.EnumInfo) string {
	return HeaderReferenceFile(e.DefLocation.Filename) + "#" + enumHtmlId(e)
}

func enumFullName(e *clangdoc.EnumInfo) string {
	return getScopeQualifier(e.Namespace, true) + e.Name
}

func writeEnumDeclaration(e *clangdoc.EnumInfo, f io.Writer) {
	writePreHeader(f)

	nsBegin, nsEnd := getNamespaceDecl(e.Namespace)
	fmt.Fprintf(f, "%s", nsBegin)

	if e.Scoped {
		fmt.Fprintf(f, "<span class=\"kwd\">enum class</span> ")
	} else {
		fmt.Fprintf(f, "<span class=\"kwd\">enum</span> ")
	}

	// Only include nested class scopes in the name, since the namespaces were already
	// included.
	fmt.Fprintf(f, "<span class=\"typ\">%s%s</span>", getScopeQualifier(e.Namespace, false), e.Name)
	if e.BaseType.Type.Name != "" {
		// Has an enum base.
		typeName, _ := getEscapedTypeName(e.BaseType.Type)
		fmt.Fprintf(f, " : <span class=\"typ\">%s</span>", typeName)
	}
	fmt.Fprintf(f, " {\n")

	for _, m := range e.Members {
		fmt.Fprintf(f, "  %s", m.Name)
		if m.Expr != "" {
			fmt.Fprintf(f, " = %s", m.Expr)
		}

		// Only show the resulting value when it's different than the expression.
		// Otherwise you can get "kFoo = 1, // = 1" which looks bad.
		if m.Value != m.Expr {
			fmt.Fprintf(f, ", <span class=\"com\">// = %s</span>\n", m.Value)
		} else {
			fmt.Fprintf(f, ",\n")
		}
	}

	fmt.Fprintf(f, "};\n%s", nsEnd)
	writePreFooter(f)
}

func writeEnumSection(settings WriteSettings, index *Index, e *clangdoc.EnumInfo, f io.Writer) {
	headingLine, _ := extractCommentHeading1(e.Description)
	if headingLine == "" {
		// TODO include namespace information.
		fmt.Fprintf(f, "## %s Enum {:#%s}\n\n", e.Name, enumHtmlId(e))
	} else {
		// Explicit title. Add a "#" to make it "level 2".
		fmt.Fprintf(f, "#%s {:#%s}\n\n", headingLine, enumHtmlId(e))
	}

	fmt.Fprintf(f, "[Declaration source code](%s)\n\n", settings.locationSourceLink(e.DefLocation))

	if !commentContains(e.Description, NoDeclTag) {
		writeEnumDeclaration(e, f)
	}
	writeComment(index, e.Description, markdownHeading2, f)

	fmt.Fprintf(f, "\n")
}
