// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

type Generator struct {
	tmpls *template.Template
}

func NewGenerator() *Generator {
	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(sourceFileTmpl))
	template.Must(tmpls.Parse(bitsTmpl))
	template.Must(tmpls.Parse(constTmpl))
	template.Must(tmpls.Parse(enumTmpl))
	template.Must(tmpls.Parse(protocolTmpl))
	template.Must(tmpls.Parse(serviceTmpl))
	template.Must(tmpls.Parse(structTmpl))
	template.Must(tmpls.Parse(unionTmpl))
	template.Must(tmpls.Parse(tableTmpl))
	template.Must(tmpls.Parse(resultTmpl))
	template.Must(tmpls.Parse(bitsTmpl))
	return &Generator{
		tmpls: tmpls,
	}
}

func (gen *Generator) GenerateImpl(wr io.Writer, tree Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "GenerateSourceFile", tree)
}

func (gen *Generator) GenerateFidl(fidl types.Root, outputFilename, rustfmtPath, rustfmtConfigPath string) error {
	tree := Compile(fidl)
	if err := os.MkdirAll(filepath.Dir(outputFilename), os.ModePerm); err != nil {
		return err
	}

	generated, err := os.Create(outputFilename)
	if err != nil {
		return err
	}

	var args []string
	if rustfmtConfigPath != "" {
		args = append(args, "--config-path", rustfmtConfigPath)
	}
	generatedPipe, err := common.NewFormatter(rustfmtPath, args...).FormatPipe(generated)
	if err != nil {
		return err
	}

	if err := gen.GenerateImpl(generatedPipe, tree); err != nil {
		return err
	}

	return generatedPipe.Close()
}
