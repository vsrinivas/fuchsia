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
	testBase *string
}

var _ cpp.CodegenOptions = (*flagsDef)(nil)

func (f flagsDef) IncludeBase() string {
	return *f.CommonFlags.IncludeBase
}

func (f flagsDef) IncludeStem() string {
	return *f.CommonFlags.IncludeStem
}

func (f flagsDef) Header() string {
	return *f.CommonFlags.Header
}

func (f flagsDef) UnifiedSourceLayout() bool {
	return true
}

var flags = flagsDef{
	CommonFlags: cpp.CommonFlags{
		Json: flag.String("json", "",
			"relative path to the FIDL intermediate representation."),
		Header: flag.String("header", "",
			"the output path for the generated header."),
		Source: flag.String("source", "",
			"the output path for the generated C++ implementation."),
		IncludeBase: flag.String("include-base", "",
			"[optional] the directory relative to which includes will be computed. "+
				"If omitted, assumes #include <fidl/library/name/llcpp/fidl.h>"),
		IncludeStem: flag.String("include-stem", "llcpp/fidl",
			"[optional] the suffix after library path when referencing includes. "+
				"Includes will be of the form <my/library/{include-stem}.h>. "),
		ClangFormatPath: flag.String("clang-format-path", "",
			"path to the clang-format tool."),
	},
	testBase: flag.String("test-base", "",
		"the output path for the generated test base header."),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.Json != "" && f.Header() != "" && *f.Source != "" && *f.testBase != ""
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

	primaryHeader, err := cpp.CalcPrimaryHeader(flags, fidl.Name.Parts())
	if err != nil {
		log.Fatal(err)
	}

	tree := cpp.CompileLL(fidl, cpp.HeaderOptions{
		PrimaryHeader: primaryHeader,
		IncludeStem:   flags.IncludeStem(),
	})

	generator := codegen.NewGenerator()
	if err := generator.GenerateHeader(tree, flags.Header(), *flags.ClangFormatPath); err != nil {
		log.Fatalf("Error running header generator: %s", err)
	}
	if err := generator.GenerateSource(tree, *flags.Source, *flags.ClangFormatPath); err != nil {
		log.Fatalf("Error running source generator: %s", err)
	}
	if err := generator.GenerateTestBase(tree, *flags.testBase, *flags.ClangFormatPath); err != nil {
		log.Fatalf("Error running test base generator: %s", err)
	}
}
