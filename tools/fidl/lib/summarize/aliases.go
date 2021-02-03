// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// alias represents an element corresponding to a FIDL alias declaration.
type alias struct {
	named
	notMember
}

// String implements Element.
func (a alias) String() string {
	return fmt.Sprintf("alias %v", a.Name())
}

// addAliases adds the aliases from the declaration map.
func (s *summarizer) addAliases(decls fidlgen.DeclMap) {
	for d, t := range decls {
		// Aliases only make an appearance in the decls section, where they are
		// registered as "type_alias".
		if t != "type_alias" {
			continue
		}
		s.addElement(alias{
			named: named{name: string(d)},
		})
	}
}
