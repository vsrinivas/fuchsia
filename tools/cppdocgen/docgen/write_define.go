// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"io"
)

// Interface for sorting the record list by function name.
type defineByName []*Define

func (f defineByName) Len() int {
	return len(f)
}
func (f defineByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f defineByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

func writeDefineGroupSection(settings WriteSettings, index *Index, g *DefineGroup, f io.Writer) {
	// Devsite uses {:#htmlId} to give the title a custom ID.
	htmlId := defineGroupHtmlId(*g)
	if len(g.ExplicitTitle) != 0 {
		fmt.Fprintf(f, "## %s {:#%s}\n\n", g.ExplicitTitle, htmlId)
	} else if len(g.Defines[0].ParamString) == 0 {
		fmt.Fprintf(f, "## %s macro {:#%s}\n\n", g.Defines[0].Name, htmlId)
	} else if g.Defines[0].ParamString == "()" {
		fmt.Fprintf(f, "## %s() macro {:#%s}\n\n", g.Defines[0].Name, htmlId)
	} else {
		fmt.Fprintf(f, "## %s(…) macro {:#%s}\n\n", g.Defines[0].Name, htmlId)
	}

	fmt.Fprintf(f, "[Declaration source code](%s)\n\n",
		settings.locationSourceLink(g.Defines[0].Location))

	writeDefineDeclarationBlock(g.Defines, f)

	// If the comment has a heading, it will have been extracted and used as the title so we
	// need to strip that to avoid duplicating.
	_, commentWithNoH1 := extractCommentHeading1(g.Defines[0].Description)
	writeComment(commentWithNoH1, markdownHeading2, f)
	fmt.Fprintf(f, "\n")
}

func defineHtmlId(d Define) string {
	// Use the define name as the ID (may need changing in the future).
	return d.Name
}

func defineGroupHtmlId(g DefineGroup) string {
	// Devsite only supports one ID per heading, so just use the first.
	return defineHtmlId(*g.Defines[0])
}

// This assumes the define destination will exist.
func defineLink(d Define) string {
	return HeaderReferenceFile(d.Location.Filename) + "#" + defineHtmlId(d)
}

func defineGroupLink(g DefineGroup) string {
	return HeaderReferenceFile(g.Defines[0].Location.Filename) + "#" + defineGroupHtmlId(g)
}

func writeDefineDeclarationBlock(defines []*Define, f io.Writer) {
	writePreHeader(f)

	for _, d := range defines {
		fmt.Fprintf(f, "<span class=\"kwd\">#define</span> <span class=\"lit\">%s</span>", d.Name)
		if len(d.ParamString) > 0 {
			fmt.Fprintf(f, "%s", d.ParamString)
		}

		if len(d.Value) > 0 {
			if d.Value[len(d.Value)-1] == byte('\\') {
				// A multiline macro definition, replace the "\" with "...".
				fmt.Fprintf(f, " %s...\n", d.Value[:len(d.Value)-1])
			} else {
				fmt.Fprintf(f, " %s\n", d.Value)
			}
		}
	}

	writePreFooter(f)
}
