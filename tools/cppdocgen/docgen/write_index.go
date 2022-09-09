// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"io"
	"sort"
)

// The functions are grouped and these groups can only have one anchor. So we need to
// process the function index by group to be able to link to the correct destination.
//
// Note that our current scheme doesn't handle overloading well. This will end up linking
// only the first instance.
type indexLink struct {
	Name string
	Link string
}

type indexLinkByName []indexLink

func (links indexLinkByName) Len() int {
	return len(links)
}
func (links indexLinkByName) Swap(i, j int) {
	links[i], links[j] = links[j], links[i]
}
func (links indexLinkByName) Less(i, j int) bool {
	return links[i].Name < links[j].Name
}

func writeFunctionIndex(index *Index, f io.Writer) {
	fmt.Fprintf(f, "## Functions\n\n")

	// Collect function info by group.
	allFuncs := make([]indexLink, 0, len(index.Functions))
	for _, header := range index.Headers {
		for _, g := range header.FunctionGroups {
			link := functionGroupLink(g)
			for _, fn := range g.Funcs {
				allFuncs = append(allFuncs, indexLink{Name: functionFullName(fn), Link: link})
			}
		}
	}

	sort.Sort(indexLinkByName(allFuncs))

	for i, link := range allFuncs {
		// Only print out the first function names if there are multiples.
		if i == 0 || allFuncs[i-1].Name != link.Name {
			fmt.Fprintf(f, "  - [%s](%s)\n", link.Name, link.Link)
		}
	}
	fmt.Fprintf(f, "\n")
}

func writeDefineIndex(index *Index, f io.Writer) {
	fmt.Fprintf(f, "## Macros\n\n")

	// Collect define info by group.
	allDefines := make([]indexLink, 0, len(index.Defines))
	for _, header := range index.Headers {
		for _, g := range header.DefineGroups {
			link := defineGroupLink(*g)
			for _, d := range g.Defines {
				allDefines = append(allDefines, indexLink{Name: d.Name, Link: link})
			}
		}
	}

	sort.Sort(indexLinkByName(allDefines))

	for _, link := range allDefines {
		fmt.Fprintf(f, "  - [%s](%s)\n", link.Name, link.Link)
	}

	fmt.Fprintf(f, "\n")
}

func WriteIndex(settings WriteSettings, index *Index, f io.Writer) {
	fmt.Fprintf(f, "# %s\n\n", settings.LibName)

	fmt.Fprintf(f, "## Header files\n\n")

	headers := make([]string, len(index.Headers))
	curHeader := 0
	for _, h := range index.Headers {
		n := h.ReferenceFileName()
		headers[curHeader] = fmt.Sprintf("  - [%s](%s)\n", settings.GetUserIncludePath(h.Name), n)
		curHeader++
	}
	sort.Strings(headers)
	for _, h := range headers {
		fmt.Fprintf(f, "%s", h)
	}
	fmt.Fprintf(f, "\n")

	if len(index.Records) > 0 {
		fmt.Fprintf(f, "## Classes\n\n")
		for _, r := range index.AllRecords() {
			// TODO(brettw) include class/struct/enum type when clang-doc is fixed.
			fmt.Fprintf(f, "  - [%s](%s)\n", recordFullName(r), recordLink(index, r))
		}
		fmt.Fprintf(f, "\n")
	}

	if len(index.Functions) > 0 {
		writeFunctionIndex(index, f)
	}

	if len(index.Defines) > 0 {
		writeDefineIndex(index, f)
	}
}
