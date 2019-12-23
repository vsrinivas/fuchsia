// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note that the go backend package is named 'golang' since 'go' is a
// keyword.
package golang

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/golang/templates"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("GoTemplates")
	template.Must(tmpls.Parse(templates.Bits))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Library))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.XUnion))
	template.Must(tmpls.Parse(templates.Table))
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// writeGoLibrary writes the FIDL generated code as a Go library.
//
// That is, at libpath, it creates a directory which contains the library
// as a Go package. This package contains only a single generated file called
// impl.go.
func (gen FidlGenerator) writeGoLibrary(libpath string, tree ir.Root) error {
	// Generate package directory.
	if err := os.MkdirAll(libpath, os.ModePerm); err != nil {
		return err
	}

	// Generate the implementation of the library.
	impl, err := gen.GenerateImplDotGo(tree)
	if err != nil {
		return err
	}

	// Write out files.
	files := map[string][]byte{
		"impl.go":  impl,
		"pkg_name": []byte(tree.PackageName),
	}
	for filename, data := range files {
		if err := ioutil.WriteFile(filepath.Join(libpath, filename), data, 0666); err != nil {
			return err
		}
	}

	return nil
}

func (gen *FidlGenerator) GenerateImplDotGo(tree ir.Root) ([]byte, error) {
	buf := new(bytes.Buffer)
	tmpl := gen.tmpls.Lookup("GenerateLibraryFile")
	if err := tmpl.Execute(buf, tree); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)
	return gen.writeGoLibrary(config.OutputBase, tree)
}
