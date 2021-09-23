// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"log"
	"strings"
	"text/template"

	fidlgen "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type Generator struct {
	*cpp.Generator
}

func NewGenerator(clangFormatPath string) Generator {
	return Generator{cpp.NewGenerator("LibFuzzer", clangFormatPath, template.FuncMap{
		"DoubleColonToUnderscore": func(s string) string {
			s2 := strings.ReplaceAll(s, "::", "_")
			// Drop any leading "::" => "_".
			if len(s2) > 0 && s2[0] == '_' {
				return s2[1:]
			}
			return s2
		},
		"Protocols":            protocols,
		"CountDecoderEncoders": countDecoderEncoders,
	}, []string{
		tmplBits,
		tmplDecoderEncoder,
		tmplDecoderEncoderHeader,
		tmplDecoderEncoderSource,
		tmplEnum,
		tmplHeader,
		tmplProtocolDecoderEncoders,
		tmplSource,
		tmplStruct,
		tmplTable,
		tmplUnion,
	})}
}

// Config is the configuration data passed to the libfuzzer generator.
type Config interface {
	cpp.CodegenOptions
	Source() string
	DecoderEncoderHeader() string
	DecoderEncoderSource() string
	HlcppBindingsIncludeStem() string
	WireBindingsIncludeStem() string
}

// Generate generates all files required for the C++ libfuzzer code.
func (gen Generator) Generate(fidl fidlgen.Root, c Config) {
	tree := cpp.CompileLibFuzzer(fidl, headerOptions(fidl.Name, c))

	gen.GenerateFiles("", tree, []cpp.GeneratedFile{
		{c.Header(), "Header"},
		{c.Source(), "Source"},
	})

	tree.HeaderOptions = headerOptions(fidl.Name, decoderEncoderCodegenOptions{c})

	gen.GenerateFiles("", tree, []cpp.GeneratedFile{
		{c.DecoderEncoderHeader(), "DecoderEncoderHeader"},
		{c.DecoderEncoderSource(), "DecoderEncoderSource"},
	})
}

func headerOptions(name fidlgen.EncodedLibraryIdentifier, c Config) cpp.HeaderOptions {
	primaryHeader, err := cpp.CalcPrimaryHeader(c, name.Parts())
	if err != nil {
		log.Fatalf("CalcPrimaryHeader failed: %v", err)
	}
	return cpp.HeaderOptions{
		PrimaryHeader:            primaryHeader,
		IncludeStem:              c.IncludeStem(),
		HlcppBindingsIncludeStem: c.HlcppBindingsIncludeStem(),
		WireBindingsIncludeStem:  c.WireBindingsIncludeStem(),
	}
}

func protocols(decls []cpp.Kinded) []cpp.Protocol {
	protocols := make([]cpp.Protocol, 0, len(decls))
	for _, decl := range decls {
		if decl.Kind() == cpp.Kinds.Protocol {
			protocols = append(protocols, decl.(cpp.Protocol))
		}
	}
	return protocols
}

// countDecoderEncoders duplicates template logic that inlines protocol, struct, and table
// decode/encode callbacks to get a count of total callbacks.
func countDecoderEncoders(decls []cpp.Kinded) int {
	count := 0
	for _, decl := range decls {
		if p, ok := decl.(cpp.Protocol); ok {
			for _, method := range p.Methods {
				if method.HasRequest {
					count++
				}
				if method.HasResponse {
					count++
				}
			}
		} else if _, ok := decl.(cpp.Struct); ok {
			count++
		} else if _, ok := decl.(cpp.Table); ok {
			count++
		}
	}
	return count
}

// decoderEncoderCodegenOptions is a forwarding CodegenOptions that changes the
// primary header to the decoder-encoder header.
type decoderEncoderCodegenOptions struct {
	Config
}

var _ cpp.CodegenOptions = (*decoderEncoderCodegenOptions)(nil)

func (o decoderEncoderCodegenOptions) Header() string {
	// When generating decoder-encoders, the primary header is the decoder-encoder header.
	return o.Config.DecoderEncoderHeader()
}
