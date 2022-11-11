// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"io"
	"path/filepath"
	"sort"
)

// Represents everything declared in this header.
type Header struct {
	Name string

	Functions []*clangdoc.FunctionInfo
	Records   []*clangdoc.RecordInfo
	Enums     []*clangdoc.EnumInfo

	// These are extracted from the header source code.
	Description []clangdoc.CommentInfo // Header-wide docstring.
	Defines     []*Define
	LineClasses []LineClass

	// Functions and defines with grouping rules applied.
	FunctionGroups []*FunctionGroup
	DefineGroups   []*DefineGroup
}

// Interface for sorting a headers by the Name.
type headerByName []*Header

func (f headerByName) Len() int {
	return len(f)
}
func (f headerByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f headerByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

// Returns the file name relative to the generated docs root directory containing the reference for
// the header with the given path.
func HeaderReferenceFile(h string) string {
	// This uses the short name of each header for the output name. In real life this will
	// lead to collisions as in libasync "lib/async-loop/loop.h" and
	// "lib/async-loop/cpp/loop.h".
	//
	// I think we should instead base the header reference file name on the include path that
	// users will use, in this case "lib_async-loop_loop.h" or something. This seems better than
	// a disambiguation scheme when there are collisions because the header file name is always
	// stable (good for external references to the file).
	return filepath.Base(h) + ".md"
}

// Computes the output reference file name for the header file, relative to this library's doc
// directory.
func (h *Header) ReferenceFileName() string {
	return HeaderReferenceFile(h.Name)
}

// Returns the custom title, if any, that is set on the header. Will return null if there is no
// custom title. It will not have any leading markdown markers ("#").
func (h *Header) CustomTitle() string {
	title, _ := extractCommentHeading1(h.Description)
	return trimMarkdownHeadings(title)
}

func WriteHeaderReference(settings WriteSettings, index *Index, h *Header, f io.Writer) {
	// When the header docstring starts with a markdown "heading 1", use that as the title,
	// otherwise generate our own.
	headerTitle, headerComment := extractCommentHeading1(h.Description)
	if headerTitle == "" {
		// Default heading.
		fmt.Fprintf(f, "# \\<%s\\> in %s\n\n", settings.GetUserIncludePath(h.Name), settings.LibName)
	} else {
		fmt.Fprintf(f, "%s\n\n", headerTitle)
	}

	fmt.Fprintf(f, "[Header source code](%s)\n\n", settings.fileSourceLink(h.Name))

	writeComment(index, headerComment, markdownHeading0, f)

	// Defines.
	for _, d := range h.DefineGroups {
		writeDefineGroupSection(settings, index, d, f)
	}

	// Enums.
	for _, e := range h.Enums {
		writeEnumSection(settings, index, e, f)
	}

	// TODO typedefs, usings.

	// Structs and classes.
	sort.Sort(recordByName(h.Records))
	for _, r := range h.Records {
		writeRecordReference(settings, index, h, r, f)
	}

	// Functions.
	for _, g := range h.FunctionGroups {
		writeFunctionGroupSection(settings, index, g, f)
	}
}
