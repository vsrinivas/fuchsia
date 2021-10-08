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

type Generator struct {
	*fidlgen.Generator
}

func NewGenerator() Generator {
	return Generator{fidlgen.NewGenerator("SyzkallerTemplates", templates,
		fidlgen.NewFormatter(""), template.FuncMap{})}
}

func (g Generator) GenerateSyscallDescription(filename string, root fidlgen.Root) error {
	return g.GenerateFile(filename, "GenerateSyscallDescription", compile(root))
}
