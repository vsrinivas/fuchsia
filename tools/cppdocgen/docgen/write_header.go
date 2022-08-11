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
	Name      string
	Functions []*clangdoc.FunctionInfo
	Records   []*clangdoc.RecordInfo

	// These two are extracted from the header source code.
	Description []clangdoc.CommentInfo // Header-wide docstring.
	Defines     []Define
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

func WriteHeaderReference(settings WriteSettings, index *Index, h *Header, f io.Writer) {
	// When the header docstring starts with a markdown "heading 1", use that as the title,
	// otherwise generate our own.
	headerTitle, headerComment := extractCommentHeading1(h.Description)
	if len(headerTitle) == 0 {
		// Default heading.
		fmt.Fprintf(f, "# \\<%s\\> in %s\n\n", settings.GetUserIncludePath(h.Name), settings.LibName)
	} else {
		fmt.Fprintf(f, "%s\n\n", headerTitle)
	}

	fmt.Fprintf(f, "[Header source code](%s)\n\n", settings.fileSourceLink(h.Name))

	writeComment(headerComment, markdownHeading0, f)

	// Don't sort the defines. Many defines have a sequence of numeric values that
	// has some reasonable meaning, and alphabetizing the list makes things look weird.
	for _, d := range h.Defines {
		writeDefineReference(settings, index, d, f)
	}

	// TODO other types and enums.

	if len(h.Records) > 0 {
		sort.Sort(recordByName(h.Records))
		for _, r := range h.Records {
			writeRecordReference(settings, index, r, f)
		}
	}

	if len(h.Functions) > 0 {
		sort.Sort(functionByName(h.Functions))
		for _, fn := range h.Functions {
			writeFunctionSection(fn, f)
			fmt.Fprintf(f, "\n")
		}
		fmt.Fprintf(f, "\n")
	}

}
