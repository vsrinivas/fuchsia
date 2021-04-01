// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type Generator struct {
	tmpls *template.Template
}

func NewGenerator() *Generator {
	tmpls := template.New("UnifiedCPPTemplates").
		Funcs(cpp.CommonTemplateFuncs)
	templates := []string{
		fragmentConstTmpl,
		fragmentTypeAliasTmpl,
		fileHeaderTmpl,
		fileSourceTmpl,
	}
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}
	return &Generator{
		tmpls: tmpls,
	}
}

func generateFile(filename, clangFormatPath string, contentGenerator func(wr io.Writer) error) error {
	if err := os.MkdirAll(filepath.Dir(filename), os.ModePerm); err != nil {
		return err
	}

	file, err := fidlgen.NewLazyWriter(filename)
	if err != nil {
		return err
	}

	generatedPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(file)
	if err != nil {
		return err
	}

	if err := contentGenerator(generatedPipe); err != nil {
		return err
	}

	return generatedPipe.Close()
}

func (gen *Generator) generateHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

func (gen *Generator) generateSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
}

// GenerateHeader generates the unified C++ bindings header, and writes it into
// the target filename.
func (gen *Generator) GenerateHeader(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateHeader(wr, tree)
	})
}

// GenerateSource generates the unified C++ bindings source, and writes it into
// the target filename.
func (gen *Generator) GenerateSource(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateSource(wr, tree)
	})
}
