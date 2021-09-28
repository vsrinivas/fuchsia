// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"text/template"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type Generator struct {
	*cpp.Generator
}

func NewGenerator(flags *cpp.CmdlineFlags) *cpp.Generator {
	return cpp.NewGenerator(flags, template.FuncMap{}, []string{
		fragmentConstTmpl,
		fragmentTypeAliasTmpl,
		fileHeaderTmpl,
		fileSourceTmpl,
	})
}
