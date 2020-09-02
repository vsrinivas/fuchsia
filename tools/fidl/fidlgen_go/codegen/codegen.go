// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"go/format"
	"io/ioutil"
	"os"
	"path/filepath"
	"text/template"
)

type Generator struct {
	implDotGoTmpl *template.Template
}

func NewGenerator() *Generator {
	tmpls := template.New("GoTemplates")
	template.Must(tmpls.Parse(bitsTmpl))
	template.Must(tmpls.Parse(enumTmpl))
	template.Must(tmpls.Parse(protocolTmpl))
	template.Must(tmpls.Parse(libraryTmpl))
	template.Must(tmpls.Parse(structTmpl))
	template.Must(tmpls.Parse(unionTmpl))
	template.Must(tmpls.Parse(tableTmpl))
	return &Generator{
		implDotGoTmpl: tmpls.Lookup("GenerateLibraryFile"),
	}
}

func (gen *Generator) generateImplDotGo(tree Root) ([]byte, error) {
	buf := new(bytes.Buffer)
	if err := gen.implDotGoTmpl.Execute(buf, tree); err != nil {
		return nil, err
	}
	return format.Source(buf.Bytes())
}

func (gen *Generator) generateFile(dataFn func() ([]byte, error), filename string) error {
	// Generate package directory.
	if err := os.MkdirAll(filepath.Dir(filename), os.ModePerm); err != nil {
		return err
	}

	// Generate data.
	data, err := dataFn()
	if err != nil {
		return err
	}

	// Write out file.
	if err := ioutil.WriteFile(filename, data, 0666); err != nil {
		return err
	}

	return nil
}

func (gen *Generator) GenerateImplFile(tree Root, filename string) error {
	return gen.generateFile(func() ([]byte, error) {
		return gen.generateImplDotGo(tree)
	}, filename)
}

func (gen *Generator) GeneratePkgNameFile(tree Root, filename string) error {
	return gen.generateFile(func() ([]byte, error) {
		return []byte(tree.PackageName), nil
	}, filename)
}
