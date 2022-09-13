// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zither

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func LowerCaseWithUnderscores(el Element) string {
	decl, ok := el.(Decl)
	if ok {
		return fidlgen.ToSnakeCase(decl.GetName().DeclarationName())
	}
	return fidlgen.ToSnakeCase(el.(Member).GetName())
}

func UpperCaseWithUnderscores(el Element) string {
	return strings.ToUpper(LowerCaseWithUnderscores(el))
}

func UpperCamelCase(el Element) string {
	decl, ok := el.(Decl)
	if ok {
		return fidlgen.ToUpperCamelCase(decl.GetName().DeclarationName())
	}
	return fidlgen.ToUpperCamelCase(el.(Member).GetName())
}
