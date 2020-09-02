// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_syzkaller/ir"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_syzkaller/templates"
)

var syzDotTxtTmpl = func() *template.Template {
	tmpls := template.New("SyzkallerTemplates")
	template.Must(tmpls.Parse(templates.SyscallDescription))
	template.Must(tmpls.Parse(templates.Protocol))
	template.Must(tmpls.Parse(templates.Struct))
	template.Must(tmpls.Parse(templates.Union))
	return tmpls.Lookup("GenerateSyscallDescription")
}()

func Compile(w io.Writer, root types.Root) error {
	return syzDotTxtTmpl.Execute(w, ir.Compile(root))
}
