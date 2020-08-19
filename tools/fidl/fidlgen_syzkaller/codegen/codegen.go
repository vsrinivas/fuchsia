// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"io/ioutil"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_syzkaller/ir"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_syzkaller/templates"
)

type Generator struct {
	syzDotTxtTmpl *template.Template
}

func NewGenerator() *Generator {
	tmpls := template.New("SyzkallerTemplates")
	template.Must(tmpls.Parse(templates.SyscallDescription))
	template.Must(tmpls.Parse(templates.Protocol))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))
	return &Generator{
		syzDotTxtTmpl: tmpls.Lookup("GenerateSyscallDescription"),
	}
}

func (gen *Generator) generate(tree ir.Root) ([]byte, error) {
	buf := new(bytes.Buffer)
	if err := gen.syzDotTxtTmpl.Execute(buf, tree); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func (gen *Generator) GenerateSyzDotTxt(tree ir.Root, filename string) error {
	data, err := gen.generate(tree)
	if err != nil {
		return err
	}
	return ioutil.WriteFile(filename, data, 0666)
}
