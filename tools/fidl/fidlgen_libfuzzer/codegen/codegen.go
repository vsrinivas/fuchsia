// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	fidlgen "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("LibFuzzer").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return cpp.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
		"NEq":   func(a interface{}, b interface{}) bool { return a != b },
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
	})

	template.Must(tmpls.Parse(tmplBits))
	template.Must(tmpls.Parse(tmplDecoderEncoder))
	template.Must(tmpls.Parse(tmplDecoderEncoderHeader))
	template.Must(tmpls.Parse(tmplDecoderEncoderSource))
	template.Must(tmpls.Parse(tmplEnum))
	template.Must(tmpls.Parse(tmplHeader))
	template.Must(tmpls.Parse(tmplProtocolDecoderEncoders))
	template.Must(tmpls.Parse(tmplSource))
	template.Must(tmpls.Parse(tmplStruct))
	template.Must(tmpls.Parse(tmplTable))
	template.Must(tmpls.Parse(tmplUnion))

	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// GenerateHeader generates the C++ libfuzzer traits for FIDL types.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// GenerateSource generates the C++ fuzzer implementation protocols in the FIDL file.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
}

// GenerateDecoderEncoderHeader generates the C++ libfuzzer traits for FIDL types.
func (gen *FidlGenerator) GenerateDecoderEncoderHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "DecoderEncoderHeader", tree)
}

// GenerateDecoderEncoderSource generates the C++ fuzzer implementation protocols in the FIDL file.
func (gen *FidlGenerator) GenerateDecoderEncoderSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "DecoderEncoderSource", tree)
}

// Config is the configuration data passed to the libfuzzer generator.
type Config interface {
	cpp.CodegenOptions
	Source() string
	DecoderEncoderHeader() string
	DecoderEncoderSource() string
}

// GenerateFidl generates all files required for the C++ libfuzzer code.
func (gen FidlGenerator) GenerateFidl(fidl fidlgen.Root, c Config, clangFormatPath string) error {
	options, headers := headerOptions(fidl.Name, c.IncludeStem())
	tree := cpp.CompileLibFuzzer(fidl, options)
	tree.Headers = headers
	if err := os.MkdirAll(filepath.Dir(c.Header()), os.ModePerm); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(c.Source()), os.ModePerm); err != nil {
		return err
	}
	if err := gen.GenerateFuzzer(fidl, tree, c, clangFormatPath); err != nil {
		return err
	}

	options, headers = headerOptionsForDecoderEncoders(fidl.Name, c.IncludeStem())
	tree.HeaderOptions = options
	tree.Headers = headers
	if err := os.MkdirAll(filepath.Dir(c.DecoderEncoderHeader()), os.ModePerm); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(c.DecoderEncoderSource()), os.ModePerm); err != nil {
		return err
	}
	if err := gen.GenerateDecoderEncoders(fidl, tree, c, clangFormatPath); err != nil {
		return err
	}

	return nil
}

func (gen FidlGenerator) GenerateFuzzer(fidl fidlgen.Root, tree cpp.Root, c Config, clangFormatPath string) error {
	headerFile, err := fidlgen.NewLazyWriter(c.Header())
	if err != nil {
		return err
	}

	headerFormatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(headerFile)
	if err != nil {
		return err
	}
	defer headerFormatterPipe.Close()

	if err := gen.GenerateHeader(headerFormatterPipe, tree); err != nil {
		return err
	}

	// Note that if the FIDL library defines no protocols, this will produce an empty file.
	sourceFile, err := fidlgen.NewLazyWriter(c.Source())
	if err != nil {
		return err
	}

	sourceFormatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(sourceFile)
	if err != nil {
		return err
	}
	defer sourceFormatterPipe.Close()

	if len(fidl.Protocols) > 0 {
		mthdCount := 0
		for _, protocol := range fidl.Protocols {
			mthdCount += len(protocol.Methods)
		}
		if mthdCount == 0 {
			return fmt.Errorf("No non-empty protocols in FIDL library: %s", string(fidl.Name))
		}

		if err := gen.GenerateSource(sourceFormatterPipe, tree); err != nil {
			return err
		}
	}

	return nil
}

func (gen FidlGenerator) GenerateDecoderEncoders(fidl fidlgen.Root, tree cpp.Root, c Config, clangFormatPath string) error {
	headerFile, err := fidlgen.NewLazyWriter(c.DecoderEncoderHeader())
	if err != nil {
		return err
	}

	headerFormatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(headerFile)
	if err != nil {
		return err
	}
	defer headerFormatterPipe.Close()

	if err := gen.GenerateDecoderEncoderHeader(headerFormatterPipe, tree); err != nil {
		return err
	}

	// Note that if the FIDL library defines no protocols, this will produce an empty file.
	sourceFile, err := fidlgen.NewLazyWriter(c.DecoderEncoderSource())
	if err != nil {
		return err
	}

	sourceFormatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(sourceFile)
	if err != nil {
		return err
	}
	defer sourceFormatterPipe.Close()

	return gen.GenerateDecoderEncoderSource(sourceFormatterPipe, tree)
}

func headerOptions(name fidlgen.EncodedLibraryIdentifier, includeStem string) (cpp.HeaderOptions, []string) {
	pkgPath := strings.Replace(string(name), ".", "/", -1)
	return cpp.HeaderOptions{
		PrimaryHeader: pkgPath + "/" + includeStem + ".h",
		IncludeStem:   includeStem,
	}, []string{pkgPath}
}

func headerOptionsForDecoderEncoders(name fidlgen.EncodedLibraryIdentifier, includeStem string) (cpp.HeaderOptions, []string) {
	pkgPath := strings.Replace(string(name), ".", "/", -1)
	return cpp.HeaderOptions{
		PrimaryHeader: pkgPath + "/" + includeStem + "_decode_encode.h",
		IncludeStem:   includeStem,
	}, []string{pkgPath}
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
