// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/cpp/templates"
	"fidl/compiler/backend/types"
	"os"
	"path/filepath"
	"text/template"
)

type FidlGenerator struct{}

func generateHeader(headerPath string, tmpls *template.Template, tree ir.Root) error {
	f, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer f.Close()

	err = tmpls.ExecuteTemplate(f, "GenerateHeaderPreamble", tree)
	if err != nil {
		return err
	}

	for _, d := range tree.Decls {
		err = d.ForwardDeclaration(tmpls, f)
	}

	for _, d := range tree.Decls {
		err = d.Declaration(tmpls, f)
	}

	err = tmpls.ExecuteTemplate(f, "GenerateHeaderPostamble", tree)
	if err != nil {
		return err
	}

	err = tmpls.ExecuteTemplate(f, "GenerateTraitsPreamble", tree)
	if err != nil {
		return err
	}

	for _, d := range tree.Decls {
		err = d.Traits(tmpls, f)
	}

	err = tmpls.ExecuteTemplate(f, "GenerateTraitsPostamble", tree)
	if err != nil {
		return err
	}

	return nil
}

func generateImplementation(implementationPath string, tmpls *template.Template, tree ir.Root) error {
	f, err := os.Create(implementationPath)
	if err != nil {
		return err
	}
	defer f.Close()

	err = tmpls.ExecuteTemplate(f, "GenerateImplementationPreamble", tree)
	if err != nil {
		return err
	}

	for _, d := range tree.Decls {
		err = d.Definition(tmpls, f)
	}

	err = tmpls.ExecuteTemplate(f, "GenerateImplementationPostamble", tree)
	if err != nil {
		return err
	}

	return nil
}

func (_ FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	relStem, err := filepath.Rel(config.RootGenDir, config.FidlStem)
	if err != nil {
		return err
	}

	headerPath := config.FidlStem + ".cc.h"
	implementationPath := config.FidlStem + ".cc"
	tree.PrimaryHeader = relStem + ".cc.h"

	tmpls := template.New("CPPTemplates")
	template.Must(tmpls.Parse(templates.Const))
	template.Must(tmpls.Parse(templates.Enum))
	template.Must(tmpls.Parse(templates.Header))
	template.Must(tmpls.Parse(templates.Implementation))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))

	err = generateHeader(headerPath, tmpls, tree)
	if err != nil {
		return err
	}

	err = generateImplementation(implementationPath, tmpls, tree)
	if err != nil {
		return err
	}

	return nil
}
