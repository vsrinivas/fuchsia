// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// bits represents the type declaration for the bits primitive.
type bits struct {
	wraparoundType
	notMember
}

// addBits adds the API elements associated with the bits declarations.
func (s *summarizer) addBits(allBits []fidlgen.Bits) {
	for _, b := range allBits {
		for i := range b.Members {
			s.addElement(newIsMember(&s.symbols, b.Name, b.Members[i].Name, fidlgen.BitsDeclType, &b.Members[i].Value))
		}
		s.addElement(
			bits{
				wraparoundType: wraparoundType{
					named:      newNamed(&s.symbols, b.Name),
					subtype:    b.Type.PrimitiveSubtype,
					strictness: b.Strictness,
					parentType: fidlgen.BitsDeclType,
				},
			})
	}
}
