// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"io"
	"sort"
)

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
		fmt.Fprintf(f, "## Functions\n\n")
		for _, fn := range index.AllFunctions() {
			fmt.Fprintf(f, "  - [%s](%s)\n", functionFullName(fn), functionLink(fn))
		}
		fmt.Fprintf(f, "\n")
	}

	if len(index.Defines) > 0 {
		fmt.Fprintf(f, "## Macros\n\n")
		for _, d := range index.AllDefines() {
			fmt.Fprintf(f, "  - [%s](%s)\n", d.Name, defineLink(index, d))
		}
		fmt.Fprintf(f, "\n")
	}
}
