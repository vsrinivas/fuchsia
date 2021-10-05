// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"embed"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//go:embed *.tmpl
var templates embed.FS

type FidlGenerator struct{ *fidlgen.Generator }

func NewFidlGenerator(dart string) FidlGenerator {
	formatter := fidlgen.NewFormatter(dart, "--no-analytics", "format", "-o", "show")
	return FidlGenerator{
		fidlgen.NewGenerator("DartTemplates", templates, formatter, template.FuncMap{}),
	}
}

func (gen FidlGenerator) GenerateAsyncFile(tree Root, path string) error {
	return gen.GenerateFile(path, "GenerateAsyncFile", tree)
}

func (gen FidlGenerator) GenerateTestFile(tree Root, path string) error {
	return gen.GenerateFile(path, "GenerateTestFile", tree)
}
