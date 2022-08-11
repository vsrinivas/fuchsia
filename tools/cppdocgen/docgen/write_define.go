// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"io"
)

// Interface for sorting the record list by function name.
type defineByName []Define

func (f defineByName) Len() int {
	return len(f)
}
func (f defineByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f defineByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

func writeDefineReference(settings WriteSettings, index *Index, d Define, f io.Writer) {
	// Devsite uses {:#htmlId} to give the title a custom ID.
	if len(d.ParamString) == 0 {
		fmt.Fprintf(f, "## %s macro {:#%s}\n\n", d.Name, defineHtmlId(d))
	} else if d.ParamString == "()" {
		fmt.Fprintf(f, "## %s() macro {:#%s}\n\n", d.Name, defineHtmlId(d))
	} else {
		fmt.Fprintf(f, "## %s(…) macro {:#%s}\n\n", d.Name, defineHtmlId(d))
	}

	fmt.Fprintf(f, "[Declaration source code](%s)\n\n", settings.locationSourceLink(d.Location))

	writeDefineDeclarationBlock(d, f)
	writeComment(d.Description, markdownHeading2, f)
}

func defineHtmlId(d Define) string {
	// Use the define name as the ID (may need changing in the future).
	return d.Name
}

// This assumes the define destination will exist.
func defineLink(index *Index, d Define) string {
	return HeaderReferenceFile(d.Location.Filename) + "#" + defineHtmlId(d)
}

func writeDefineDeclarationBlock(d Define, f io.Writer) {
	writePreHeader(f)

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

	writePreFooter(f)
}
