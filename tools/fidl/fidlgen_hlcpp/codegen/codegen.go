// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"log"
	"os"
	"path/filepath"
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

type FidlGenerator struct {
	tmpls *template.Template
	CodeGenerationMode
}

func NewFidlGenerator(mode CodeGenerationMode) *FidlGenerator {
	tmpls := template.New("CPPTemplates").Funcs(cpp.MergeFuncMaps(cpp.CommonTemplateFuncs,
		template.FuncMap{
			"IncludeDomainObjects": func() bool {
				return mode == Monolithic ||
					mode == OnlyGenerateDomainObjects
			},
			"IncludeProxiesAndStubs": func() bool { return mode == Monolithic },
		}))

	// Natural types templates
	template.Must(tmpls.Parse(bitsTemplate))
	template.Must(tmpls.Parse(constTemplate))
	template.Must(tmpls.Parse(enumTemplate))
	template.Must(tmpls.Parse(protocolTemplateNaturalTypes))
	template.Must(tmpls.Parse(structTemplate))
	template.Must(tmpls.Parse(tableTemplate))
	template.Must(tmpls.Parse(unionTemplate))

	// Proxies and stubs templates
	template.Must(tmpls.Parse(protocolTemplateProxiesAndStubs))
	template.Must(tmpls.Parse(serviceTemplate))

	// File templates
	template.Must(tmpls.Parse(headerTemplate))
	template.Must(tmpls.Parse(implementationTemplate))
	template.Must(tmpls.Parse(testBaseTemplate))
	return &FidlGenerator{
		tmpls:              tmpls,
		CodeGenerationMode: mode,
	}
}

// GenerateHeader generates the C++ bindings header.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// GenerateSource generates the C++ bindings source, i.e. implementation.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Implementation", tree)
}

func (gen *FidlGenerator) GenerateTestBase(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "TestBase", tree)
}

func (gen *FidlGenerator) generateFile(filepath, clangFormatPath string, generate func(w io.Writer) error) error {
	file, err := fidlgen.NewLazyWriter(filepath)
	if err != nil {
		return err
	}

	formatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(file)
	if err != nil {
		return err
	}

	if err := generate(formatterPipe); err != nil {
		return err
	}

	return formatterPipe.Close()
}

// GenerateFidl generates all files required for the C++ bindings.
func (gen *FidlGenerator) GenerateFidl(fidl fidlgen.Root, opts Config, clangFormatPath string) error {
	primaryHeader, err := cpp.CalcPrimaryHeader(opts, fidl.Name.Parts())
	if err != nil {
		log.Fatal(err)
	}
	tree := cpp.CompileHL(fidl, cpp.HeaderOptions{
		PrimaryHeader: primaryHeader,
		IncludeStem:   opts.IncludeStem(),
	})

	if err := os.MkdirAll(filepath.Dir(opts.Header()), os.ModePerm); err != nil {
		return err
	}
	err = gen.generateFile(opts.Header(), clangFormatPath, func(w io.Writer) error {
		return gen.GenerateHeader(w, tree)
	})
	if err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(opts.Source()), os.ModePerm); err != nil {
		return err
	}
	err = gen.generateFile(opts.Source(), clangFormatPath, func(w io.Writer) error {
		return gen.GenerateSource(w, tree)
	})
	if err != nil {
		return err
	}

	if gen.CodeGenerationMode == OnlyGenerateDomainObjects {
		// TestBase header only applies to protocols definitions.
		return nil
	}

	if err := os.MkdirAll(filepath.Dir(opts.TestBase()), os.ModePerm); err != nil {
		return err
	}
	return gen.generateFile(opts.TestBase(), clangFormatPath, func(w io.Writer) error {
		return gen.GenerateTestBase(w, tree)
	})
}
