// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// These correspond to templated classes forward-declared in
// //src/lib/fidl/include/lib/fidl/cpp/internal/natural_types.h
var (
	TypeTraits = internalNs.member("TypeTraits")
)

type Struct struct {
	Attributes
	fidlgen.Resourceness
	nameVariants
	AnonymousChildren   []ScopedLayout
	CodingTableType     name
	Members             []StructMember
	BackingBufferTypeV1 string
	BackingBufferTypeV2 string
	Result              *Result
	// Full decls needed to check if a type is memcpy compatible.
	// Only set if it may be possible for a type to be memcpy compatible,
	// e.g. has no padding.
	// See the struct template for usage.
	FullDeclMemcpyCompatibleDeps []nameVariants

	TypeShapeV1 TypeShape
	TypeShapeV2 TypeShape

	// TypeTraits contains information about a natural domain object.
	TypeTraits name

	isEmptyStruct                bool
	isAnonymousRequestOrResponse bool
}

func (*Struct) Kind() declKind {
	return Kinds.Struct
}

// IsAnonymousRequestOrResponse indicates whether this struct is used as a method
// request/response.
func (s *Struct) IsAnonymousRequestOrResponse() bool {
	return s.isAnonymousRequestOrResponse
}

var _ Kinded = (*Struct)(nil)
var _ namespaced = (*Struct)(nil)

type StructMember struct {
	Attributes
	nameVariants
	Type              Type
	DefaultValue      ConstantValue
	OffsetV1          int
	OffsetV2          int
	HandleInformation *HandleInformation
}

func (m StructMember) AsParameter() Parameter {
	return Parameter{
		nameVariants:      m.nameVariants,
		Type:              m.Type,
		OffsetV1:          m.OffsetV1,
		OffsetV2:          m.OffsetV2,
		HandleInformation: m.HandleInformation,
	}
}

func (sm StructMember) NameAndType() (string, Type) {
	return sm.Name(), sm.Type
}

func (sm StructMember) StorageName() string {
	return sm.Name() + "_"
}

var _ Member = (*StructMember)(nil)

func (c *compiler) compileStructMember(val fidlgen.StructMember) StructMember {
	t := c.compileType(val.Type)

	defaultValue := ConstantValue{}
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type)
	}

	return StructMember{
		Attributes:        Attributes{val.Attributes},
		nameVariants:      structMemberContext.transform(val.Name),
		Type:              t,
		DefaultValue:      defaultValue,
		OffsetV1:          val.FieldShapeV1.Offset,
		OffsetV2:          val.FieldShapeV2.Offset,
		HandleInformation: c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileStruct(val fidlgen.Struct, anonMessageBody bool) *Struct {
	name := c.compileNameVariants(val.Name)
	codingTableType := name.Wire.ns.member(c.compileCodingTableType(val.Name))
	r := Struct{
		Attributes:        Attributes{val.Attributes},
		AnonymousChildren: c.getAnonymousChildren(val.Layout),
		TypeShapeV1:       TypeShape{val.TypeShapeV1},
		TypeShapeV2:       TypeShape{val.TypeShapeV2},
		Resourceness:      val.Resourceness,
		nameVariants:      name,
		CodingTableType:   codingTableType,
		Members:           []StructMember{},
		BackingBufferTypeV1: computeAllocation(
			TypeShape{val.TypeShapeV1}.MaxTotalSize(), boundednessBounded).
			BackingBufferType(),
		BackingBufferTypeV2: computeAllocation(
			TypeShape{val.TypeShapeV2}.MaxTotalSize(), boundednessBounded).
			BackingBufferType(),
		TypeTraits:                   TypeTraits.template(name.Unified),
		isAnonymousRequestOrResponse: anonMessageBody,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	if len(r.Members) == 0 {
		r.isEmptyStruct = true
		r.Members = []StructMember{
			c.compileStructMember(fidlgen.EmptyStructMember("__reserved")),
		}
	}

	// Construct a deduped list of decls for IsMemcpyCompatible template definitions.
	seen := make(map[string]struct{})
	for _, member := range r.Members {
		if _, ok := seen[member.Type.HLCPP.String()]; ok {
			continue
		}
		seen[member.Type.HLCPP.String()] = struct{}{}

		// The dangerous identifiers test package contains identifiers that won't compile.
		// e.g. ::fidl::test::dangerous::struct::types::camel::Interface gives an
		// "expected unqualified-id" error because of "struct".
		// There isn't an easily accessible dangerous identifiers list to replace identifiers.
		if strings.Contains(member.Type.HLCPP.String(), "::fidl::test::dangerous::") {
			continue
		}

		r.FullDeclMemcpyCompatibleDeps = append(r.FullDeclMemcpyCompatibleDeps, member.Type.nameVariants)
	}

	return &r
}
