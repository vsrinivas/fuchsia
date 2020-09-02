// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

var syzDotTxtTmpl = func() *template.Template {
	tmpls := template.New("SyzkallerTemplates")
	template.Must(tmpls.Parse(syscallDescriptionTmpl))
	template.Must(tmpls.Parse(protocolTmpl))
	template.Must(tmpls.Parse(structTmpl))
	template.Must(tmpls.Parse(unionTmpl))
	return tmpls.Lookup("GenerateSyscallDescription")
}()

func Compile(w io.Writer, root types.Root) error {
	return syzDotTxtTmpl.Execute(w, compile(root))
}
