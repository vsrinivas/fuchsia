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

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/cpp"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
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
		"Protocols": protocols,
	})

	template.Must(tmpls.Parse(tmplBits))
	template.Must(tmpls.Parse(tmplEnum))
	template.Must(tmpls.Parse(tmplHeader))
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

// GenerateFidl generates all files required for the C++ libfuzzer code.
func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config, clangFormatPath string) error {
	tree := cpp.CompileLibFuzzer(fidl)
	prepareTree(fidl.Name, &tree)

	headerPath := config.OutputBase + ".h"
	sourcePath := config.OutputBase + ".cc"
	baseDir := filepath.Dir(config.OutputBase)

	if err := os.MkdirAll(baseDir, os.ModePerm); err != nil {
		return err
	}

	headerFile, err := os.Create(headerPath)
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
	sourceFile, err := os.Create(sourcePath)
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

func prepareTree(name types.EncodedLibraryIdentifier, tree *cpp.Root) {
	pkgPath := strings.Replace(string(name), ".", "/", -1)
	tree.PrimaryHeader = pkgPath + "/cpp/libfuzzer.h"
	tree.Headers = []string{pkgPath + "/cpp/fidl.h"}
}

func protocols(decls []cpp.Decl) []*cpp.Protocol {
	protocols := make([]*cpp.Protocol, 0, len(decls))
	for _, decl := range decls {
		if protocol, ok := decl.(*cpp.Protocol); ok {
			protocols = append(protocols, protocol)
		}
	}
	return protocols
}
