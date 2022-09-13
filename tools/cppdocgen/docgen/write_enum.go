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
		fmt.Fprintf(f, "<span class=\"kwd\">enum </span> ")
	}

	// Only include nested class scopes in the name, since the namespaces were already
	// included.
	fmt.Fprintf(f, "<span class=\"typ\">%s%s</span>", getScopeQualifier(e.Namespace, false), e.Name)
	// TODO include explicit type information like " : int32", this needs clang-doc support.
	fmt.Fprintf(f, " {\n")

	for _, m := range e.Members {
		// TODO include the value of the enum. This needs clang-doc support. I'm thinking
		// it would be good to know if the value was explicitly set or not because sometimes
		// the values have meaning. So for an explicitly set value we would get:
		//   kValue1 = 1,
		// But maybe if the value isn't explicitly set we can still record what it is:
		//   kValue2,  // = 2
		fmt.Fprintf(f, "  %s,\n", m)
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

	writeEnumDeclaration(e, f)
	writeComment(e.Description, markdownHeading2, f)

	fmt.Fprintf(f, "\n")
}
