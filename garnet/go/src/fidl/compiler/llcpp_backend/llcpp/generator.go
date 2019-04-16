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
	"fidl/compiler/llcpp_backend/llcpp/templates/files"
	"fidl/compiler/llcpp_backend/llcpp/templates/fragments"
	"fidl/compiler/backend/types"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("LLCPPTemplates").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return ir.Kinds },
		"Eq": func(a interface{}, b interface{}) bool { return a == b },
	})
	templates := []string {
		fragments.Bits,
		fragments.Const,
		fragments.Enum,
		fragments.Interface,
		fragments.ReplyCFlavor,
		fragments.ReplyCallerAllocate,
		fragments.ReplyInPlace,
		fragments.SendEventCFlavor,
		fragments.SendEventCallerAllocate,
		fragments.SendEventInPlace,
		fragments.Struct,
		fragments.SyncRequestCFlavor,
		fragments.SyncRequestCallerAllocate,
		fragments.SyncRequestInPlace,
		fragments.SyncServer,
		fragments.Table,
		fragments.Union,
		fragments.XUnion,
		files.Header,
		files.Source,
	}
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}
	return &FidlGenerator{
		tmpls: tmpls,
	}
}

// GenerateHeader generates the C++ bindings header.
func (gen *FidlGenerator) GenerateHeader(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// GenerateSource generates the C++ bindings source, i.e. implementation.
func (gen *FidlGenerator) GenerateSource(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
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
	sourcePath := config.OutputBase + ".cc"

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
