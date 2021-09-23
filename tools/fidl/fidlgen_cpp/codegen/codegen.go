// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"text/template"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type Generator struct {
	*cpp.Generator
}

func NewGenerator(clangFormatPath string) Generator {
	return Generator{cpp.NewGenerator("UnifiedCPPTemplates", clangFormatPath, template.FuncMap{}, []string{
		fragmentConstTmpl,
		fragmentTypeAliasTmpl,
		fileHeaderTmpl,
		fileSourceTmpl,
	})}
}

func (gen Generator) Generate(tree cpp.Root, header string, source string) {
	gen.GenerateFiles("", tree, []cpp.GeneratedFile{
		{header, "Header"},
		{source, "Source"},
	})
}
