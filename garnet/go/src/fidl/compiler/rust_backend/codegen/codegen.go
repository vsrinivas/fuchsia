// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/types"
	"fidl/compiler/rust_backend/ir"
	"fidl/compiler/rust_backend/templates"
)

type Generator struct {
	tmpls *template.Template
}

func NewGenerator() *Generator {
	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(templates.SourceFile))
	template.Must(tmpls.Parse(templates.Bits))
	template.Must(tmpls.Parse(templates.Const))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Service))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))
	template.Must(tmpls.Parse(templates.Table))
	template.Must(tmpls.Parse(templates.Result))
	template.Must(tmpls.Parse(templates.Bits))
	return &Generator{
		tmpls: tmpls,
	}
}

func (gen *Generator) GenerateImpl(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateSourceFile", tree)
}

func (gen *Generator) GenerateFidl(fidl types.Root, outputFilename string) error {
	tree := ir.Compile(fidl)
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
