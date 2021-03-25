// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Enum struct {
	fidlgen.Attributes
	fidlgen.Strictness
	NameVariants
	Enum    fidlgen.Enum
	Type    NameVariants
	Members []EnumMember
}

func (Enum) Kind() declKind {
	return Kinds.Enum
}

var _ Kinded = (*Enum)(nil)

func (e Enum) UnknownValueForTmpl() interface{} {
	return e.Enum.UnknownValueForTmpl()
}

type EnumMember struct {
	fidlgen.EnumMember
	Name  string
	Value ConstantValue
}

func (c *compiler) compileEnum(val fidlgen.Enum) Enum {
	name := c.compileNameVariants(val.Name)
	r := Enum{
		Attributes:   val.Attributes,
		Strictness:   val.Strictness,
		NameVariants: name,
		Enum:         val,
		Type:         NameVariantsForPrimitive(val.Type),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			EnumMember: v,
			Name:       changeIfReserved(v.Name),
			// TODO(fxbug.dev/7660): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, nil, fidlgen.Type{
				Kind:             fidlgen.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
		})
	}
	return r
}
