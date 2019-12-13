// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/rust/ir"
	"fidl/compiler/backend/rust/templates"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(templates.SourceFile))
	template.Must(tmpls.Parse(templates.Bits))
	template.Must(tmpls.Parse(templates.Const))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Service))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.XUnion))
	template.Must(tmpls.Parse(templates.Table))
	template.Must(tmpls.Parse(templates.Result))
	template.Must(tmpls.Parse(templates.Bits))
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

func (gen *FidlGenerator) GenerateImpl(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateSourceFile", tree)
}

func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)
	outputFilename := config.OutputBase + ".rs"
	if err := os.MkdirAll(filepath.Dir(outputFilename), os.ModePerm); err != nil {
		return err
	}
	f, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer f.Close()
	return gen.GenerateImpl(f, tree)
}
