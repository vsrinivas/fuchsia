// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package backend

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_dart/backend/ir"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_dart/backend/templates"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("DartTemplates")
	template.Must(tmpls.Parse(templates.Bits))
	template.Must(tmpls.Parse(templates.Const))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Library))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Table))
	template.Must(tmpls.Parse(templates.Union))
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

func (gen FidlGenerator) generateAsyncFile(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateAsyncFile", tree)
}

func (gen FidlGenerator) generateTestFile(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateTestFile", tree)
}

func writeFile(
	generate func(io.Writer, ir.Root) error, tree ir.Root,
	outputFilename string, dartfmt string) error {

	if err := os.MkdirAll(filepath.Dir(outputFilename), os.ModePerm); err != nil {
		return err
	}
	generated, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer generated.Close()

	generatedPipe, err := common.NewFormatter(dartfmt).FormatPipe(generated)
	if err != nil {
		return err
	}

	if err := generate(generatedPipe, tree); err != nil {
		return err
	}
	return generatedPipe.Close()
}

func (gen FidlGenerator) GenerateAsyncFile(tree ir.Root, path string, dartfmt string) error {
	return writeFile(gen.generateAsyncFile, tree, path, dartfmt)
}

func (gen FidlGenerator) GenerateTestFile(tree ir.Root, path string, dartfmt string) error {
	return writeFile(gen.generateTestFile, tree, path, dartfmt)
}
