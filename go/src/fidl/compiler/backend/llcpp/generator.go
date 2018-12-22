// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/llcpp/templates"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("LLCPPTemplates")
	template.Must(tmpls.Parse(templates.Const))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Header))
	template.Must(tmpls.Parse(templates.Implementation))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// GenerateHeader generates the C++ bindings header.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree ir.Root) error {
	tmpls := gen.tmpls
	if err := tmpls.ExecuteTemplate(wr, "GenerateHeaderPreamble", tree); err != nil {
		return err
	}

	for _, d := range tree.Decls {
		if err := d.ForwardDeclaration(tmpls, wr); err != nil {
			return err
		}
	}

	for _, d := range tree.Decls {
		if err := d.Declaration(tmpls, wr); err != nil {
			return err
		}
	}

	if err := tmpls.ExecuteTemplate(wr, "GenerateHeaderPostamble", tree); err != nil {
		return err
	}

	if err := tmpls.ExecuteTemplate(wr, "GenerateTraitsPreamble", tree); err != nil {
		return err
	}

	for _, d := range tree.Decls {
		if err := d.Traits(tmpls, wr); err != nil {
			return err
		}
	}

	if err := tmpls.ExecuteTemplate(wr, "GenerateTraitsPostamble", tree); err != nil {
		return err
	}

	return nil
}

// GenerateSource generates the C++ bindings source, i.e. implementation.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree ir.Root) error {
	tmpls := gen.tmpls
	if err := tmpls.ExecuteTemplate(wr, "GenerateImplementationPreamble", tree); err != nil {
		return err
	}

	for _, d := range tree.Decls {
		if err := d.Definition(tmpls, wr); err != nil {
			return err
		}
	}

	if err := tmpls.ExecuteTemplate(wr, "GenerateImplementationPostamble", tree); err != nil {
		return err
	}

	return nil
}

// GenerateFidl generates all files required for the C++ bindings.
func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	relStem, err := filepath.Rel(config.IncludeBase, config.OutputBase)
	if err != nil {
		return err
	}
	tree.PrimaryHeader = relStem + ".h"

	headerPath := config.OutputBase + ".h"
	sourcePath := config.OutputBase + ".cpp"

	if err := os.MkdirAll(filepath.Dir(config.OutputBase), os.ModePerm); err != nil {
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
