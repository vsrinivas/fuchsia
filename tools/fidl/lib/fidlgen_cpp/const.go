// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//
// Combined logic for constant values and const declarations.
//

type ConstantValue struct {
	Natural string
	Wire    string
}

func (cv *ConstantValue) IsSet() bool {
	return cv.Natural != "" && cv.Wire != ""
}

func (cv *ConstantValue) String() string {
	switch currentVariant {
	case noVariant:
		fidl.TemplateFatalf(
			"Called ConstantValue.String() on %s/%s when currentVariant isn't set.\n",
			cv.Natural, cv.Wire)
	case naturalVariant:
		return string(cv.Natural)
	case wireVariant:
		return string(cv.Wire)
	}
	panic("not reached")
}

func (c *compiler) compileConstant(val fidl.Constant, t *Type, typ fidl.Type) ConstantValue {
	switch val.Kind {
	case fidl.IdentifierConstant:
		ci := fidl.ParseCompoundIdentifier(val.Identifier)
		if len(ci.Member) > 0 {
			member := changeIfReserved(ci.Member)
			ci.Member = ""
			dn := c.compileDeclName(ci.Encode())
			return ConstantValue{
				Natural: dn.Natural.String() + "::" + member,
				Wire:    dn.Wire.String() + "::" + member,
			}
		} else {
			dn := c.compileDeclName(val.Identifier)
			return ConstantValue{Natural: dn.Natural.String(), Wire: dn.Wire.String()}
		}
	case fidl.LiteralConstant:
		lit := c.compileLiteral(val.Literal, typ)
		return ConstantValue{Natural: lit, Wire: lit}
	case fidl.BinaryOperator:
		return ConstantValue{
			Natural: fmt.Sprintf("static_cast<%s>(%s)", t.TypeName.Natural, val.Value),
			Wire:    fmt.Sprintf("static_cast<%s>(%s)", t.TypeName.Wire, val.Value),
		}
		// return ConstantValue{Natural: naturalVal, Wire: wireVal}
	default:
		panic(fmt.Sprintf("unknown constant kind: %v", val.Kind))
	}
}

type Const struct {
	fidl.Attributes
	DeclName
	Extern    bool
	Decorator string
	Type      Type
	Value     ConstantValue

	// Kind is a type tag; omit when initializing the struct.
	Kind constKind
}

func (c *compiler) compileConst(val fidl.Const) Const {
	n := c.compileDeclName(val.Name)
	v := Const{Attributes: val.Attributes,
		DeclName: n,
	}
	if val.Type.Kind == fidl.StringType {
		v.Extern = true
		v.Decorator = "const"
		v.Type = Type{
			TypeName: PrimitiveTypeName("char*"),
		}
		v.Value = c.compileConstant(val.Value, nil, val.Type)
	} else {
		t := c.compileType(val.Type)
		v.Extern = false
		v.Decorator = "constexpr"
		v.Type = t
		v.Value = c.compileConstant(val.Value, &t, val.Type)
	}

	return v
}

func (c *compiler) compileLiteral(val fidl.Literal, typ fidl.Type) string {
	switch val.Kind {
	case fidl.StringLiteral:
		return fmt.Sprintf("%q", val.Value)
	case fidl.NumericLiteral:
		if val.Value == "-9223372036854775808" || val.Value == "0x8000000000000000" {
			// C++ only supports nonnegative literals and a value this large in absolute
			// value cannot be represented as a nonnegative number in 64-bits.
			return "(-9223372036854775807ll-1)"
		}
		// TODO(fxbug.dev/7810): Once we expose resolved constants for defaults, e.g.
		// in structs, we will not need ignore hex and binary values.
		if strings.HasPrefix(val.Value, "0x") || strings.HasPrefix(val.Value, "0b") {
			return val.Value
		}

		// float32 literals must be marked as such.
		if strings.ContainsRune(val.Value, '.') {
			if typ.Kind == fidl.PrimitiveType && typ.PrimitiveSubtype == fidl.Float32 {
				return fmt.Sprintf("%sf", val.Value)
			} else {
				return val.Value
			}
		}

		if !strings.HasPrefix(val.Value, "-") {
			return fmt.Sprintf("%su", val.Value)
		}
		return val.Value
	case fidl.TrueLiteral:
		return "true"
	case fidl.FalseLiteral:
		return "false"
	case fidl.DefaultLiteral:
		return "default"
	default:
		panic(fmt.Sprintf("unknown literal kind: %v", val.Kind))
	}
}
