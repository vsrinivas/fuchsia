// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note that the go backend package is named 'golang' since 'go' is a
// keyword.
package golang

import (
	"bytes"
	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/golang/templates"
	"fidl/compiler/backend/types"
	"io/ioutil"
	"os"
	"path/filepath"
	"text/template"
)

type FidlGenerator struct{}

// writeGoLibrary writes the FIDL generated code as a Go library.
//
// That is, at libpath, it creates a directory which contains the library
// as a Go package. This package contains only a single generated file called
// impl.go.
func writeGoLibrary(libpath string, tmpl *template.Template, tree ir.Root) error {
	// Generate package directory.
	if err := os.MkdirAll(libpath, os.ModePerm); err != nil {
		return err
	}

	// Generate the implementation of the library.
	filename := filepath.Join(libpath, "impl.go")
	buf := new(bytes.Buffer)
	if err := tmpl.Execute(buf, tree); err != nil {
		return err
	}
	if err := ioutil.WriteFile(filename, buf.Bytes(), 0666); err != nil {
		return err
	}

	// Write go package (library) name into a file.
	pkgfilename := filepath.Join(libpath, "pkg_name")
	return ioutil.WriteFile(pkgfilename, []byte(tree.PackageName), 0666)
}

func (_ FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	tmpls := template.New("GoTemplates")
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Library))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))

	return writeGoLibrary(config.OutputBase, tmpls.Lookup("GenerateLibraryFile"), tree)
}
