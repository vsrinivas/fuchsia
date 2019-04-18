// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package syzkaller

import (
	"bytes"
	"fidl/compiler/backend/syzkaller/ir"
	"fidl/compiler/backend/syzkaller/templates"
	"fidl/compiler/backend/types"
	"io/ioutil"
	"text/template"
)

type FidlGenerator struct {
	tmpls *template.Template
}

func NewFidlGenerator() *FidlGenerator {
	tmpls := template.New("SyzkallerTemplates")
	template.Must(tmpls.Parse(templates.SyscallDescription))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))

	return &FidlGenerator{
		tmpls: tmpls,
	}
}

func (gen FidlGenerator) writeSyscallDescription(outputFilename string, tree ir.Root) error {
	data, err := gen.GenerateImpl(tree)
	if err != nil {
		return err
	}

	return ioutil.WriteFile(outputFilename, data, 0666)
}

func (gen *FidlGenerator) GenerateImpl(tree ir.Root) ([]byte, error) {
	buf := new(bytes.Buffer)
	tmpl := gen.tmpls.Lookup("GenerateSyscallDescription")
	if err := tmpl.Execute(buf, tree); err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func (gen FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)
	srcPath := config.OutputBase + ".syz.txt"

	return gen.writeSyscallDescription(srcPath, tree)
}
