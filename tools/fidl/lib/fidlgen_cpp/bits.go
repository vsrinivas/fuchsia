// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Bits struct {
	fidlgen.Attributes
	fidlgen.Strictness
	NameVariants
	Type     NameVariants
	Mask     string
	MaskName NameVariants
	Members  []BitsMember
}

func (Bits) Kind() declKind {
	return Kinds.Bits
}

var _ Kinded = (*Bits)(nil)

type BitsMember struct {
	fidlgen.Attributes
	Name  string
	Value ConstantValue
}

func (c *compiler) compileBits(val fidlgen.Bits) Bits {
	name := c.compileNameVariants(val.Name)
	r := Bits{
		Attributes:   val.Attributes,
		Strictness:   val.Strictness,
		NameVariants: name,
		Type:         c.compileType(val.Type).NameVariants,
		Mask:         val.Mask,
		MaskName:     name.AppendName("Mask"),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, BitsMember{
			v.Attributes,
			changeIfReserved(v.Name),
			c.compileConstant(v.Value, nil, val.Type),
		})
	}
	return r
}
