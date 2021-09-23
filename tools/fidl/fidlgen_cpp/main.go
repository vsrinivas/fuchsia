// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_cpp/codegen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type flagsDef struct {
	cpp.CommonFlags
	naturalDomainObjectsIncludeStem *string
	wireBindingsIncludeStem         *string
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
	return false
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
				"If omitted, assumes #include <fidl/library/name/cpp/fidl_v2.h>"),
		IncludeStem: flag.String("include-stem", "cpp/fidl_v2",
			"[optional] the suffix after library path when referencing includes. "+
				"Includes will be of the form <my/library/{include-stem}.h>. "),
		ClangFormatPath: flag.String("clang-format-path", "",
			"path to the clang-format tool."),
	},
	naturalDomainObjectsIncludeStem: flag.String("natural-domain-objects-include-stem",
		"cpp/natural_types",
		"[optional] the path stem when including the natural domain objects header. "+
			"Includes will be of the form <my/library/{include-stem}.h>. "),
	wireBindingsIncludeStem: flag.String("wire-bindings-include-stem",
		"llcpp/fidl",
		"[optional] the path stem when including the wire bindings header. "+
			"Includes will be of the form <my/library/{include-stem}.h>. "),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.Json != "" && f.Header() != "" && *f.Source != "" &&
		*f.naturalDomainObjectsIncludeStem != "" && *f.wireBindingsIncludeStem != ""
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

	headerPath, err := filepath.Abs(flags.Header())
	if err != nil {
		log.Fatal(err)
	}

	sourcePath, err := filepath.Abs(*flags.Source)
	if err != nil {
		log.Fatal(err)
	}

	primaryHeader, err := cpp.CalcPrimaryHeader(flags, fidl.Name.Parts())
	if err != nil {
		log.Fatal(err)
	}

	tree := cpp.CompileUnified(fidl, cpp.HeaderOptions{
		PrimaryHeader:                   primaryHeader,
		IncludeStem:                     flags.IncludeStem(),
		NaturalDomainObjectsIncludeStem: *flags.naturalDomainObjectsIncludeStem,
		WireBindingsIncludeStem:         *flags.wireBindingsIncludeStem,
	})

	generator := codegen.NewGenerator(*flags.ClangFormatPath)
	generator.Generate(tree, headerPath, sourcePath)
}
