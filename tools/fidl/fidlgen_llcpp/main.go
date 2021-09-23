// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_llcpp/codegen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type flagsDef struct {
	cpp.CommonFlags
	root    *string
	library *string
}

var flags = flagsDef{
	CommonFlags: cpp.CommonFlags{
		Json: flag.String("json", "",
			"relative path to the FIDL intermediate representation."),
		ClangFormatPath: flag.String("clang-format-path", "",
			"path to the clang-format tool."),
	},
	root: flag.String("root", "",
		"the path to output generated files into."),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.Json != "" && *f.root != ""
}

func main() {
	flag.Parse()
	if !flag.Parsed() || !flags.valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	fidl, err := fidlgen.ReadJSONIr(*flags.Json)
	if err != nil {
		log.Fatal(err)
	}

	tree := cpp.CompileLL(fidl, cpp.HeaderOptions{
		IncludeStem: "cpp/wire.h",
	})

	generator := codegen.NewGenerator(*flags.ClangFormatPath)
	generator.Generate(*flags.root, tree)
}
