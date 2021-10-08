// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"embed"
	"go/format"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//go:embed *.tmpl
var templates embed.FS

type formatter struct{}

func (f formatter) Format(source []byte) ([]byte, error) {
	return format.Source(source)
}

var gofmt fidlgen.Formatter = formatter{}

type Generator struct {
	*fidlgen.Generator
}

func NewGenerator() Generator {
	return Generator{fidlgen.NewGenerator("GoTemplates", templates, gofmt,
		template.FuncMap{})}
}

func (gen Generator) GenerateImplFile(tree Root, filename string) error {
	return gen.GenerateFile(filename, "GenerateLibraryFile", tree)
}

func (gen *Generator) GeneratePkgNameFile(tree Root, filename string) error {
	return fidlgen.WriteFileIfChanged(filename, []byte(tree.PackageName))
}
