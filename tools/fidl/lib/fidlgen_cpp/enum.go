// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"math"
	"strconv"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Enum struct {
	Attributes
	fidlgen.Strictness
	nameVariants
	CodingTableType name
	Enum            fidlgen.Enum
	Type            nameVariants
	Members         []EnumMember
}

func (*Enum) Kind() declKind {
	return Kinds.Enum
}

var _ Kinded = (*Enum)(nil)
var _ namespaced = (*Enum)(nil)

func (e Enum) UnknownValueForTmpl() interface{} {
	return e.Enum.UnknownValueForTmpl()
}

// integer represents either an int or an uint.
type integer struct {
	// Positive values are stored in u.
	u uint64

	// Negative or zero values are stored in i.
	i int64
}

func (i *integer) decrement() {
	if i.u > 0 {
		i.u--
	} else {
		i.i--
	}
}

func (i *integer) asUint64() uint64 {
	if i.u > 0 {
		return i.u
	} else {
		return uint64(i.i)
	}
}

func readEnumValue(m fidlgen.EnumMember, typ fidlgen.PrimitiveSubtype) integer {
	v := m.Value.Value
	if typ.IsSigned() {
		i, err := strconv.ParseInt(v, 10, 64)
		if err != nil {
			fidlgen.TemplateFatalf("Failed to parse enum value: %s", v)
		}
		if i > 0 {
			return integer{
				u: uint64(i),
				i: 0,
			}
		}
		return integer{
			u: 0,
			i: i,
		}
	}

	u, err := strconv.ParseUint(v, 10, 64)
	if err != nil {
		fidlgen.TemplateFatalf("Failed to parse enum value: %s", v)
	}
	return integer{
		u: u,
		i: 0,
	}
}

// UnusedEnumValueForTmpl attempts to find a numerical value that is unused in
// the enumeration. Its purpose is to support forcing users of flexible enums
// to write a `default:` case, by injecting an extra ugly enum member like
// "DO_NOT_USE_THIS = {{ UnusedEnumValueForTmpl }}" whose usage is discouraged.
//
// When an enum is fully populated (e.g. a uint8 enum with 256 members), it is
// not possible to find an unused slot. This will return the designated unknown
// value. Note that by definition there cannot be unknown values in fully
// populated enums, so it's fine if the user ends up writing a switch without a
// `default:` case in that case.
func (e Enum) UnusedEnumValueForTmpl() interface{} {
	nb := e.Enum.Type.NumberOfBits()
	if math.Log2(float64(len(e.Members))) >= float64(nb) {
		return e.UnknownValueForTmpl()
	}
	// Make a set of used enum values
	memberValues := make(map[integer]struct{})
	for _, m := range e.Enum.Members {
		i := readEnumValue(m, e.Enum.Type)
		memberValues[i] = struct{}{}
	}
	// Find the first value that's unused.
	var i integer
	signed := e.Enum.Type.IsSigned()
	var minusOne uint64
	minusOne--
	minusOne = minusOne >> (64 - nb)
	if signed {
		i = integer{
			u: minusOne / 2,
			i: 0,
		}
	} else {
		i = integer{
			u: minusOne,
			i: 0,
		}
	}
	for {
		_, ok := memberValues[i]
		if !ok {
			return i.asUint64()
		}
		i.decrement()
	}
}

func (e Enum) IsCompatibleWithError() bool {
	return e.Enum.Type == fidlgen.Int32 || e.Enum.Type == fidlgen.Uint32
}

type EnumMember struct {
	Attributes
	nameVariants
	Value      ConstantValue
	EnumMember fidlgen.EnumMember
}

func (m EnumMember) IsUnknown() bool {
	return m.EnumMember.IsUnknown()
}

func (c *compiler) compileEnum(val fidlgen.Enum) *Enum {
	name := c.compileNameVariants(val.Name)
	r := Enum{
		Attributes:      Attributes{val.Attributes},
		Strictness:      val.Strictness,
		nameVariants:    name,
		CodingTableType: name.Unified.ns.member(c.compileCodingTableType(val.Name)),
		Enum:            val,
		Type:            NameVariantsForPrimitive(val.Type),
	}
	for _, v := range val.Members {
		r.Members = append(r.Members, EnumMember{
			Attributes:   Attributes{v.Attributes},
			nameVariants: enumMemberContext.transform(v.Name),
			// TODO(fxbug.dev/7660): When we expose types consistently in the IR, we
			// will not need to plug this here.
			Value: c.compileConstant(v.Value, nil, fidlgen.Type{
				Kind:             fidlgen.PrimitiveType,
				PrimitiveSubtype: val.Type,
			}),
			EnumMember: v,
		})
	}
	return &r
}
