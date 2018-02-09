// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"fidl/compiler/backend/rust/ir"
	"fidl/compiler/backend/rust/templates"
	"fidl/compiler/backend/types"
	"os"
	"path/filepath"
	"text/template"
)

type FidlGenerator struct{}

func writeFile(outputFilename string,
	templateName string,
	tmpls *template.Template,
	tree ir.Root) error {
	f, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer f.Close()
	return tmpls.ExecuteTemplate(f, templateName, tree)
}

func (_ FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	relStem, err := filepath.Rel(config.RootGenDir, config.FidlStem)
	if err != nil {
		return err
	}

	srcPath := relStem + ".rs"

	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(templates.SourceFile))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))

	return writeFile(srcPath, "GenerateSourceFile", tmpls, tree)
}
