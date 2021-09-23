// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_libfuzzer/codegen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type flagsDef struct {
	cpp.CommonFlags
	decoderEncoderHeader     *string
	decoderEncoderSource     *string
	hlcppBindingsIncludeStem *string
	wireBindingsIncludeStem  *string
}

var _ cpp.CodegenOptions = (*flagsDef)(nil)
var _ codegen.Config = (*flagsDef)(nil)

func (f flagsDef) IncludeBase() string {
	return *f.CommonFlags.IncludeBase
}

func (f flagsDef) IncludeStem() string {
	return *f.CommonFlags.IncludeStem
}

func (f flagsDef) Header() string {
	return *f.CommonFlags.Header
}

func (f flagsDef) Source() string {
	return *f.CommonFlags.Source
}

func (f flagsDef) DecoderEncoderHeader() string {
	return *f.decoderEncoderHeader
}

func (f flagsDef) DecoderEncoderSource() string {
	return *f.decoderEncoderSource
}

func (f flagsDef) HlcppBindingsIncludeStem() string {
	return *f.hlcppBindingsIncludeStem
}

func (f flagsDef) WireBindingsIncludeStem() string {
	return *f.wireBindingsIncludeStem
}

func (f flagsDef) UnifiedSourceLayout() bool {
	return false
}

var flags = flagsDef{
	CommonFlags: cpp.CommonFlags{
		Json: flag.String("json", "",
			"relative path to the FIDL intermediate representation."),
		Header: flag.String("header", "",
			"the output path for the generated fuzzer header."),
		Source: flag.String("source", "",
			"the output path for the generated fuzzer implementation."),
		IncludeBase: flag.String("include-base", "",
			"[optional] the directory relative to which includes will be computed. "+
				"If omitted, assumes #include <fidl/library/name/cpp/libfuzzer.h>"),
		IncludeStem: flag.String("include-stem", "cpp/libfuzzer",
			"[optional] the suffix after library path when referencing includes. "+
				"Includes will be of the form <my/library/{include-stem}.h>. "),
		ClangFormatPath: flag.String("clang-format-path", "",
			"path to the clang-format tool."),
	},
	decoderEncoderHeader: flag.String("decoder-encoder-header", "",
		"the output path for the generated decoder-encoder header."),
	decoderEncoderSource: flag.String("decoder-encoder-source", "",
		"the output path for the generated decoder-encoder implementation."),
	hlcppBindingsIncludeStem: flag.String("hlcpp-bindings-include-stem",
		"cpp/fidl",
		"[optional] the path stem when including the hlcpp bindings header. "+
			"Includes will be of the form <my/library/{include-stem}.h>. "),
	wireBindingsIncludeStem: flag.String("wire-bindings-include-stem",
		"llcpp/fidl",
		"[optional] the path stem when including the wire bindings header. "+
			"Includes will be of the form <my/library/{include-stem}.h>. "),
}

func (f flagsDef) valid() bool {
	return *f.Json != "" && f.Header() != "" && f.Source() != "" &&
		*f.decoderEncoderHeader != "" && *f.decoderEncoderSource != ""
}

func main() {
	flag.Parse()
	if !flag.Parsed() || !flags.valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	ir, err := fidlgen.ReadJSONIr(*flags.Json)
	if err != nil {
		log.Fatal(err)
	}

	codegen.NewGenerator(*flags.ClangFormatPath).Generate(ir, flags)
}
