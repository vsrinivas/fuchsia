// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Bits struct {
	Attributes
	fidlgen.Strictness
	nameVariants
	Type     nameVariants
	Mask     string
	MaskName nameVariants
	Members  []BitsMember
}

func (*Bits) Kind() declKind {
	return Kinds.Bits
}

var _ Kinded = (*Bits)(nil)
var _ namespaced = (*Bits)(nil)

type BitsMember struct {
	Attributes
	nameVariants
	Value ConstantValue
}

func (c *compiler) compileBits(val fidlgen.Bits) *Bits {
	name := c.compileNameVariants(val.Name)
	r := Bits{
		Attributes:   Attributes{val.Attributes},
		Strictness:   val.Strictness,
		nameVariants: name,
		Type:         c.compileType(val.Type).nameVariants,
		Mask:         val.Mask,
		MaskName:     name.appendName("Mask"),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, BitsMember{
			Attributes:   Attributes{v.Attributes},
			nameVariants: bitsMemberContext.transform(v.Name),
			Value:        c.compileConstant(v.Value, nil, val.Type),
		})
	}
	return &r
}
