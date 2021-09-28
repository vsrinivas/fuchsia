// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"text/template"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

func NewGenerator(flags *cpp.CmdlineFlags) *cpp.Generator {
	return cpp.NewGenerator(flags, template.FuncMap{
		"IncludeDomainObjects": func() bool {
			return true
		},
	}, []string{
		// Natural types templates
		bitsTemplate,
		constTemplate,
		enumTemplate,
		protocolTemplateNaturalTypes,
		structTemplate,
		tableTemplate,
		unionTemplate,
		// Proxies and stubs templates
		protocolTemplateProxiesAndStubs,
		serviceTemplate,
		// File templates
		headerTemplate,
		implementationTemplate,
		testBaseTemplate,
	})
}
