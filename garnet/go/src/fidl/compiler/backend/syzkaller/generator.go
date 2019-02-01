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

type FidlGenerator struct{}

func writeSyscallDescription(outputFilename string, tmpl *template.Template, tree ir.Root) error {
	buf := new(bytes.Buffer)
	if err := tmpl.Execute(buf, tree); err != nil {
		return err
	}
	return ioutil.WriteFile(outputFilename, buf.Bytes(), 0666)
}

func (_ FidlGenerator) GenerateFidl(fidl types.Root, config *types.Config) error {
	tree := ir.Compile(fidl)

	srcPath := config.OutputBase + ".txt"

	tmpls := template.New("SyzkallerTemplates")
	template.Must(tmpls.Parse(templates.SyscallDescription))
	template.Must(tmpls.Parse(templates.Interface))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))

	return writeSyscallDescription(srcPath, tmpls.Lookup("GenerateSyscallDescription"), tree)
}
