// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp_overnet_embedded

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/cpp_overnet_embedded/templates"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("OvernetEmbedded").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return ir.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
	})
	templates := []string{
		templates.Bits,
		templates.Const,
		templates.Enum,
		templates.Header,
		templates.Implementation,
		templates.Interface,
		templates.Struct,
		templates.Table,
		templates.TestBase,
		templates.Union,
		templates.XUnion,
	}
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// GenerateHeader generates the C++ bindings header.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// GenerateSource generates the C++ bindings source, i.e. implementation.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Implementation", tree)
}

// GenerateFidl generates all files required for the C++ bindings.
func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	relStem, err := filepath.Rel(config.IncludeBase, config.OutputBase)
	if err != nil {
		return err
	}
	tree.PrimaryHeader = relStem + ".h"

	headerPath := config.OutputBase + ".h"
	sourcePath := config.OutputBase + ".cc"

	if err := os.MkdirAll(filepath.Dir(config.OutputBase), os.ModePerm); err != nil {
		return err
	}

	headerFile, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer headerFile.Close()

	if err := gen.GenerateHeader(headerFile, tree); err != nil {
		return err
	}

	sourceFile, err := os.Create(sourcePath)
	if err != nil {
		return err
	}
	defer sourceFile.Close()

	if err := gen.GenerateSource(sourceFile, tree); err != nil {
		return err
	}

	return nil
}
