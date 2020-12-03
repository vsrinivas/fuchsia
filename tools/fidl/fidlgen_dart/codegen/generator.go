// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("DartTemplates")
	template.Must(tmpls.Parse(bitsTmpl))
	template.Must(tmpls.Parse(constTmpl))
	template.Must(tmpls.Parse(enumTmpl))
	template.Must(tmpls.Parse(protocolTmpl))
	template.Must(tmpls.Parse(libraryTmpl))
	template.Must(tmpls.Parse(structTmpl))
	template.Must(tmpls.Parse(tableTmpl))
	template.Must(tmpls.Parse(unionTmpl))
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

func (gen FidlGenerator) generateAsyncFile(wr io.Writer, tree Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateAsyncFile", tree)
}

func (gen FidlGenerator) generateTestFile(wr io.Writer, tree Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateTestFile", tree)
}

func writeFile(
	generate func(io.Writer, Root) error, tree Root,
	outputFilename string, dartfmt string) error {

	if err := os.MkdirAll(filepath.Dir(outputFilename), os.ModePerm); err != nil {
		return err
	}
	generated, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer generated.Close()

	generatedPipe, err := fidl.NewFormatter(dartfmt).FormatPipe(generated)
	if err != nil {
		return err
	}

	if err := generate(generatedPipe, tree); err != nil {
		return err
	}
	return generatedPipe.Close()
}

func (gen FidlGenerator) GenerateAsyncFile(tree Root, path string, dartfmt string) error {
	return writeFile(gen.generateAsyncFile, tree, path, dartfmt)
}

func (gen FidlGenerator) GenerateTestFile(tree Root, path string, dartfmt string) error {
	return writeFile(gen.generateTestFile, tree, path, dartfmt)
}
