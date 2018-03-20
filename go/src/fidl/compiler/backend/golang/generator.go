// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note that the go backend package is named 'golang' since 'go' is a
// keyword.
package golang

import (
	"bytes"
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/golang/templates"
	"fidl/compiler/backend/types"
	"go/format"
	"io/ioutil"
	"text/template"
)

type FidlGenerator struct{}

func writeGoFile(filename string, tmpl *template.Template, tree ir.Root) error {
	buf := new(bytes.Buffer)
	if err := tmpl.Execute(buf, tree); err != nil {
		return err
	}
	data, err := format.Source(buf.Bytes())
	if err != nil {
		// Write the unformatted source, so the user has something
		// to compare to the line number in the error message.
		ioutil.WriteFile(filename, buf.Bytes(), 0666)
		return err
	}
	return ioutil.WriteFile(filename, data, 0666)
}

func (_ FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	tmpls := template.New("GoTemplates").Funcs(template.FuncMap{
		"privatize": common.ToLowerCamelCase,
	})
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Library))
	template.Must(tmpls.Parse(templates.Struct))

	libraryPath := config.FidlStem + ".go"
	return writeGoFile(libraryPath, tmpls.Lookup("GenerateLibraryFile"), tree)
}
