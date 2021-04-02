// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

const aConstType Kind = "const"

// addConsts obtains the API elements present in the constants.
func (s *summarizer) addConsts(consts []fidlgen.Const) {
	for _, c := range consts {
		s.addElement(
			aConst{
				named: named{name: Name(c.Name)},
				aType: c.Type,
			})
	}
}

// aConst represents a constant Element.
type aConst struct {
	named
	notMember
	aType fidlgen.Type
}

// String implements Element
func (c aConst) String() string {
	return c.Serialize().String()
}

func (c aConst) Serialize() ElementStr {
	e := c.named.Serialize()
	e.Kind = Kind(aConstType)
	e.Decl = Decl(fidlTypeString(c.aType))
	return e
}
