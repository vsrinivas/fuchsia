// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp_libfuzzer

import (
	"io"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/cpp_libfuzzer/templates"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("LibFuzzer").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return ir.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
		"NEq":   func(a interface{}, b interface{}) bool { return a != b },
	})

	template.Must(tmpls.Parse(templates.Bits))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Header))
	template.Must(tmpls.Parse(templates.Source))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Table))
	template.Must(tmpls.Parse(templates.Union))
	template.Must(tmpls.Parse(templates.XUnion))

	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// GenerateHeader generates the C++ libfuzzer traits for FIDL types.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// GenerateSource generates the placeholder C++ source file for the libfuzzer fidl_cpp build target.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
}

// GenerateFidl generates all files required for the C++ libfuzzer code.
func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)
	prepareTree(fidl.Name, &tree)

	headerPath := config.OutputBase + ".h"
	sourcePath := config.OutputBase + ".cc"
	baseDir := filepath.Dir(config.OutputBase)

	if err := os.MkdirAll(baseDir, os.ModePerm); err != nil {
		return err
	}

	headerFile, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer headerFile.Close()

	if err := gen.GenerateHeader(headerFile, tree); err != nil {
		return err
	}

	sourceFile, err := os.Create(sourcePath)
	if err != nil {
		return err
	}
	defer sourceFile.Close()

	if err := gen.GenerateSource(sourceFile, tree); err != nil {
		return err
	}

	return nil
}

func prepareTree(name types.EncodedLibraryIdentifier, tree *ir.Root) {
	pkgPath := strings.Replace(string(name), ".", "/", -1)
	tree.PrimaryHeader = pkgPath + "/cpp/libfuzzer.h"
	tree.Headers = []string{pkgPath + "/cpp/fidl.h"}
}
