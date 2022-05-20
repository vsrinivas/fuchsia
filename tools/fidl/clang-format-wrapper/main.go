// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A wrapper around clang-format used by golden tests. It ensures golden file
// reformatting is consistent with fidlgen_cpp, which skips formatting under
// certain conditions (see fxbug.dev/78303).
package main

import (
	"fmt"
	"io"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "Usage: %s <clang-format-path>", os.Args[0])
		os.Exit(1)
	}
	formatter := fidlgen_cpp.NewFormatter(os.Args[1])
	input, err := io.ReadAll(os.Stdin)
	if err != nil {
		log.Fatalf("reading stdin: %s", err)
	}
	output, err := formatter.Format(input)
	if err != nil {
		log.Fatalf("formatting input: %s", err)
	}
	_, err = os.Stdout.Write(output)
	if err != nil {
		log.Fatalf("writing output: %s", err)
	}
}
