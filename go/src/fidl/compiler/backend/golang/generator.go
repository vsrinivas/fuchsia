// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note that the go backend package is named 'golang' since 'go' is a
// keyword.
package golang

import (
	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/golang/templates"
	"fidl/compiler/backend/types"
	"os"
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

	tmpls := template.New("GoTemplates")
	template.Must(tmpls.Parse(templates.Library))

	libraryPath := config.FidlStem + ".go"
	return writeFile(libraryPath, "GenerateLibraryFile", tmpls, tree)
}
