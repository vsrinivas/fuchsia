// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// alias represents an element corresponding to a FIDL alias declaration.
type alias struct {
	named
	notMember
}

const aliasType Kind = "alias"

func (a *alias) Serialize() ElementStr {
	e := a.named.Serialize()
	e.Kind = aliasType
	return e
}

// addAliases adds the aliases from the declaration map.
//
// TODO(fxbug.dev/110289): Add aliases to summaries. This is currently unused.
func (s *summarizer) addAliases(decls fidlgen.DeclMap) {
	for d, t := range decls {
		// Aliases only make an appearance in the decls section, where they are
		// registered as "alias".
		if t != "alias" {
			continue
		}
		s.addElement(&alias{
			named: named{name: Name(d)},
		})
	}
}
