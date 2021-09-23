// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"log"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

// CodeGenerationMode controls which subset of bindings code to generate.
type CodeGenerationMode int

const (
	_ CodeGenerationMode = iota

	// Monolithic generates all of HLCPP bindings in one header/source pair.
	Monolithic

	// OnlyGenerateDomainObjects generates only the domain objects
	// (structs, tables, unions, enums, bits, request/response codecs, etc.)
	OnlyGenerateDomainObjects

	// TODO(fxbug.dev/64093): Add a third generation mode which only generates
	// proxies and stubs.
)

// Config is the configuration data passed to the HLCPP generator.
type Config interface {
	Header() string
	Source() string
	TestBase() string
	// The directory to which C and C++ includes should be relative.
	IncludeBase() string
	// The path suffix after the library path when referencing includes.
	IncludeStem() string
	// Use the unified binding source layout.
	UnifiedSourceLayout() bool
}

type Generator struct {
	*cpp.Generator
	CodeGenerationMode
}

func NewGenerator(mode CodeGenerationMode, clangFormatPath string) Generator {
	return Generator{
		Generator: cpp.NewGenerator("CPPTemplates", clangFormatPath, template.FuncMap{
			"IncludeDomainObjects": func() bool {
				return mode == Monolithic ||
					mode == OnlyGenerateDomainObjects
			},
			"IncludeProxiesAndStubs": func() bool { return mode == Monolithic },
		}, []string{
			// Natural types templates
			bitsTemplate,
			constTemplate,
			enumTemplate,
			protocolTemplateNaturalTypes,
			structTemplate,
			tableTemplate,
			unionTemplate,
			// Proxies and stubs templates
			protocolTemplateProxiesAndStubs,
			serviceTemplate,
			// File templates
			headerTemplate,
			implementationTemplate,
			testBaseTemplate,
		}),
		CodeGenerationMode: mode,
	}
}

// GenerateFidl generates all files required for the C++ bindings.
func (gen Generator) GenerateFidl(fidl fidlgen.Root, opts Config) {
	primaryHeader, err := cpp.CalcPrimaryHeader(opts, fidl.Name.Parts())
	if err != nil {
		log.Fatal(err)
	}
	tree := cpp.CompileHL(fidl, cpp.HeaderOptions{
		PrimaryHeader: primaryHeader,
		IncludeStem:   opts.IncludeStem(),
	})

	files := []cpp.GeneratedFile{
		{opts.Header(), "Header"},
		{opts.Source(), "Implementation"},
	}
	if gen.CodeGenerationMode != OnlyGenerateDomainObjects {
		// TestBase header only applies to protocols definitions.
		files = append(files, cpp.GeneratedFile{opts.TestBase(), "TestBase"})
	}

	gen.GenerateFiles("", tree, files)
}
