// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Enum struct {
	fidl.Attributes
	fidl.Strictness
	DeclName
	Enum    fidl.Enum
	Type    TypeName
	Members []EnumMember

	// Kind is a type tag; omit when initializing the struct.
	Kind enumKind
}

func (e *Enum) UnknownValueForTmpl() interface{} {
	return e.Enum.UnknownValueForTmpl()
}

type EnumMember struct {
	fidl.EnumMember
	Name  string
	Value ConstantValue
}

func (c *compiler) compileEnum(val fidl.Enum) Enum {
	name := c.compileDeclName(val.Name)
	r := Enum{
		Attributes: val.Attributes,
		Strictness: val.Strictness,
		DeclName:   name,
		Enum:       val,
		Type:       TypeNameForPrimitive(val.Type),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			EnumMember: v,
			Name:       changeIfReserved(v.Name),
			// TODO(fxbug.dev/7660): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, nil, fidl.Type{
				Kind:             fidl.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
		})
	}
	return r
}
