// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/cpp"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("CPPTemplates").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return cpp.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
	})
	template.Must(tmpls.Parse(bitsTemplate))
	template.Must(tmpls.Parse(constTemplate))
	template.Must(tmpls.Parse(enumTemplate))
	template.Must(tmpls.Parse(headerTemplate))
	template.Must(tmpls.Parse(implementationTemplate))
	template.Must(tmpls.Parse(protocolTemplate))
	template.Must(tmpls.Parse(serviceTemplate))
	template.Must(tmpls.Parse(structTemplate))
	template.Must(tmpls.Parse(tableTemplate))
	template.Must(tmpls.Parse(testBaseTemplate))
	template.Must(tmpls.Parse(unionTemplate))
	return &FidlGenerator{
		tmpls: tmpls,
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
	file, err := os.Create(filepath)
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
func (gen *FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config, clangFormatPath string) error {
	tree := cpp.CompileHL(fidl)

	relStem, err := filepath.Rel(config.IncludeBase, config.OutputBase)
	if err != nil {
		return err
	}
	tree.PrimaryHeader = relStem + ".h"

	headerPath := config.OutputBase + ".h"
	sourcePath := config.OutputBase + ".cc"
	testBasePath := config.OutputBase + "_test_base.h"

	if err := os.MkdirAll(filepath.Dir(config.OutputBase), os.ModePerm); err != nil {
		return err
	}

	err = gen.generateFile(headerPath, clangFormatPath, func(w io.Writer) error {
		return gen.GenerateHeader(w, tree)
	})
	if err != nil {
		return err
	}

	err = gen.generateFile(sourcePath, clangFormatPath, func(w io.Writer) error {
		return gen.GenerateSource(w, tree)
	})
	if err != nil {
		return err
	}

	return gen.generateFile(testBasePath, clangFormatPath, func(w io.Writer) error {
		return gen.GenerateTestBase(w, tree)
	})
}
