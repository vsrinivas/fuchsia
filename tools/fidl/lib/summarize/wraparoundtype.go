// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// wraparoundType is a type that adds more structure to an underlying primitive
// (wrapped) type.  All such types also support strictness.
type wraparoundType struct {
	named
	subtype    fidlgen.PrimitiveSubtype
	strictness fidlgen.Strictness
	parentType fidlgen.DeclType
}

func strictness(strict fidlgen.Strictness) string {
	if strict {
		return "strict"
	} else {
		return "flexible"
	}
}

// String implements Element.
func (b wraparoundType) String() string {
	return fmt.Sprintf("%v %v %v %v",
		strictness(b.strictness),
		b.parentType,
		b.Name(),
		b.subtype)
}
