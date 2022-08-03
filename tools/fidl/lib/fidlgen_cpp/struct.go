// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"encoding/binary"
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Struct struct {
	Attributes
	fidlgen.Resourceness
	nameVariants
	AnonymousChildren   []ScopedLayout
	CodingTableType     name
	Members             []StructMember
	PaddingV1           []StructPadding
	PaddingV2           []StructPadding
	BackingBufferTypeV1 string
	BackingBufferTypeV2 string
	IsInResult          bool
	ParametersTupleDecl name
	// Full decls needed to check if a type is memcpy compatible.
	// Only set if it may be possible for a type to be memcpy compatible,
	// e.g. has no padding.
	// See the struct template for usage.
	FullDeclMemcpyCompatibleDeps []nameVariants

	TypeShapeV1 TypeShape
	TypeShapeV2 TypeShape

	isEmptyStruct                bool
	isAnonymousRequestOrResponse bool
}

func (*Struct) Kind() declKind {
	return Kinds.Struct
}

// AsParameters flattens the struct's members into a parameter list.
func (s *Struct) AsParameters(_ *Type, _ *HandleInformation) []Parameter {
	var out []Parameter
	for _, sm := range s.Members {
		out = append(out, sm.AsParameter())
	}
	return out
}

// SetInResult marks the struct as being used in as the success variant in a
// method Result, and takes note of the tuple declaration of the result's
// parameters. Because a named struct may be in multiple results, every
// call to this function after the first one per instance is a no-op, since the
// struct would have already been marked with the same information.
func (s *Struct) SetInResult(result *Result) {
	if !s.IsInResult {
		s.IsInResult = true
		s.ParametersTupleDecl = result.ValueTupleDecl
	}
}

func (s *Struct) IsEmpty() bool {
	return s.isEmptyStruct
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
	NaturalConstraint string
	WireConstraint    string
}

var _ Member = (*StructMember)(nil)

func (sm StructMember) AsParameter() Parameter {
	return Parameter{
		nameVariants:      sm.nameVariants,
		Type:              sm.Type,
		OffsetV1:          sm.OffsetV1,
		OffsetV2:          sm.OffsetV2,
		HandleInformation: sm.HandleInformation,
		WireConstraint:    sm.WireConstraint,
	}
}

func (sm StructMember) NameAndType() (string, Type) {
	return sm.Name(), sm.Type
}

func (sm StructMember) StorageName() string {
	return sm.Name() + "_"
}

// NaturalInitializer is an expression in natural types for initializing the
// struct member within its struct field definition. May be empty if we choose
// to delegate to the default constructor of the member type.
func (sm StructMember) NaturalInitializer() string {
	var unwrapArray func(ty *Type) string
	unwrapArray = func(ty *Type) string {
		if ty.IsPrimitiveType() {
			// Zero initialize them.
			return "{}"
		}
		if ty.Kind == TypeKinds.Array {
			return unwrapArray(ty.ElementType)
		}
		return ""
	}

	if !sm.Type.Nullable {
		return unwrapArray(&sm.Type)
	}
	return ""
}

// NaturalPossiblyInvalidDefaultInitializer is an expression in natural types
// for how to default initialize the struct member within its struct field
// definition. May result in an invalid object if it has a strict union
// somewhere.
func (sm StructMember) NaturalPossiblyInvalidDefaultInitializer() string {
	if !sm.Type.Nullable {
		switch sm.Type.Kind {
		case TypeKinds.Array:
			return fmt.Sprintf("::fidl::internal::DefaultConstructPossiblyInvalidObject<%s>::Make()", sm.Type.Unified)
		case TypeKinds.Struct, TypeKinds.Table, TypeKinds.Union:
			return "::fidl::internal::DefaultConstructPossiblyInvalidObjectTag{}"
		}
	}
	return "{}"
}

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
		NaturalConstraint: t.NaturalFieldConstraint,
		WireConstraint:    t.WireFieldConstraint,
	}
}

type StructPadding struct {
	Offset   int
	MaskType string
	Mask     string
}

func toStructPadding(in fidlgen.PaddingMarker) StructPadding {
	switch len(in.Mask) {
	case 2:
		return StructPadding{
			Offset:   in.Offset,
			MaskType: "uint16_t",
			Mask:     fmt.Sprintf("0x%04x", binary.LittleEndian.Uint16(in.Mask)),
		}
	case 4:
		return StructPadding{
			Offset:   in.Offset,
			MaskType: "uint32_t",
			Mask:     fmt.Sprintf("0x%08x", binary.LittleEndian.Uint32(in.Mask)),
		}
	case 8:
		return StructPadding{
			Offset:   in.Offset,
			MaskType: "uint64_t",
			Mask:     fmt.Sprintf("0x%016xull", binary.LittleEndian.Uint64(in.Mask)),
		}
	default:
		panic("unexpected mask size")
	}
}

func toStructPaddings(in []fidlgen.PaddingMarker) []StructPadding {
	var out []StructPadding
	for _, m := range in {
		out = append(out, toStructPadding(m))
	}
	return out
}

func (c *compiler) compileStruct(val fidlgen.Struct) *Struct {
	name := c.compileNameVariants(val.Name)
	codingTableType := name.Wire.ns.member(c.compileCodingTableType(val.Name))
	r := Struct{
		Attributes:        Attributes{val.Attributes},
		AnonymousChildren: c.getAnonymousChildren(val.LayoutDecl),
		TypeShapeV1:       TypeShape{val.TypeShapeV1},
		TypeShapeV2:       TypeShape{val.TypeShapeV2},
		Resourceness:      val.Resourceness,
		nameVariants:      name,
		CodingTableType:   codingTableType,
		Members:           []StructMember{},
		BackingBufferTypeV1: computeAllocation(
			TypeShape{val.TypeShapeV1}.MaxTotalSize(), TypeShape{val.TypeShapeV1}.MaxHandles, boundednessBounded).
			BackingBufferType(),
		BackingBufferTypeV2: computeAllocation(
			TypeShape{val.TypeShapeV2}.MaxTotalSize(), TypeShape{val.TypeShapeV2}.MaxHandles, boundednessBounded).
			BackingBufferType(),
		IsInResult: false,
		PaddingV1:  toStructPaddings(val.BuildPaddingMarkers(fidlgen.WireFormatVersionV1)),
		PaddingV2:  toStructPaddings(val.BuildPaddingMarkers(fidlgen.WireFormatVersionV2)),
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
