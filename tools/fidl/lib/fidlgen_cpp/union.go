// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// UnionName stores all of the information necessary to use a union as a
// payload. Unlike structs, unions always use a single "payload" argument
// pointing to the underlying union/table type, so for externally defined unions
// we only need to store the name and (optional) owning result type of the
// union, rather than the entire, flattenable declaration with all of its
// members.
type UnionName struct {
	nameVariants
}

func (*UnionName) Kind() declKind {
	return Kinds.Union
}

// AsParameters renders the referenced union as a parameter list of length 1.
func (u *UnionName) AsParameters(ty *Type, hi *HandleInformation) []Parameter {
	return []Parameter{{
		Type:              *ty,
		nameVariants:      ty.nameVariants,
		OffsetV1:          0,
		OffsetV2:          0,
		HandleInformation: hi,
	}}
}

var _ Kinded = (*UnionName)(nil)
var _ Payloader = (*UnionName)(nil)
var _ namespaced = (*UnionName)(nil)

type Union struct {
	UnionName
	Attributes
	fidlgen.Strictness
	fidlgen.Resourceness
	CodingTableType     name
	AnonymousChildren   []ScopedLayout
	TagEnum             nameVariants
	TagUnknown          nameVariants
	TagInvalid          nameVariants
	WireOrdinalEnum     name
	WireInvalidOrdinal  name
	Members             []UnionMember
	BackingBufferTypeV1 string
	BackingBufferTypeV2 string
	TypeShapeV1         TypeShape
	TypeShapeV2         TypeShape

	// Result points to the Result this union is being used to represent, if this
	// is in fact a Result wrapper.
	Result *Result
}

var _ Kinded = (*Union)(nil)
var _ Payloader = (*Union)(nil)
var _ namespaced = (*Union)(nil)

type UnionMember struct {
	Attributes
	nameVariants
	Ordinal           uint64
	Type              Type
	StorageName       name
	TagName           nameVariants
	WireOrdinalName   name
	Offset            int
	HandleInformation *HandleInformation
	NaturalConstraint string
	WireConstraint    string
	NaturalIndex      int
}

func (um UnionMember) UpperCamelCaseName() string {
	return fidlgen.ToUpperCamelCase(um.Name())
}

func (um UnionMember) NameAndType() (string, Type) {
	return um.Name(), um.Type
}

func (c *compiler) compileUnion(val fidlgen.Union) *Union {
	name := c.compileNameVariants(val.Name)
	codingTableType := name.Wire.ns.member(c.compileCodingTableType(val.Name))
	tagEnum := name.nest("Tag")
	wireOrdinalEnum := name.Wire.nest("Ordinal")
	// HLCPP use "kUnknown", whereas the new C++ bindings use the un-spellable name.
	tagUnknown := tagEnum.nest("kUnknown")
	tagUnknownNew := tagEnum.nest("_do_not_handle_this__write_a_default_case_instead")
	tagUnknown.Wire = tagUnknownNew.Wire
	tagUnknown.Unified = tagUnknownNew.Unified
	u := Union{
		UnionName:          UnionName{nameVariants: name},
		Attributes:         Attributes{val.Attributes},
		TypeShapeV1:        TypeShape{val.TypeShapeV1},
		TypeShapeV2:        TypeShape{val.TypeShapeV2},
		AnonymousChildren:  c.getAnonymousChildren(val),
		Strictness:         val.Strictness,
		Resourceness:       val.Resourceness,
		CodingTableType:    codingTableType,
		TagEnum:            tagEnum,
		TagUnknown:         tagUnknown,
		TagInvalid:         tagEnum.nest("Invalid"),
		WireOrdinalEnum:    wireOrdinalEnum,
		WireInvalidOrdinal: wireOrdinalEnum.nest("Invalid"),
		BackingBufferTypeV1: computeAllocation(
			TypeShape{val.TypeShapeV1}.MaxTotalSize(), TypeShape{val.TypeShapeV1}.MaxHandles, boundednessBounded).
			BackingBufferType(),
		BackingBufferTypeV2: computeAllocation(
			TypeShape{val.TypeShapeV2}.MaxTotalSize(), TypeShape{val.TypeShapeV2}.MaxHandles, boundednessBounded).
			BackingBufferType(),
	}

	naturalIndex := 1
	for _, mem := range val.Members {
		if mem.Reserved {
			continue
		}
		name := unionMemberContext.transform(mem.Name)
		tag := unionMemberTagContext.transform(mem.Name)
		t := c.compileType(*mem.Type)
		u.Members = append(u.Members, UnionMember{
			Attributes:        Attributes{mem.Attributes},
			Ordinal:           uint64(mem.Ordinal),
			Type:              t,
			nameVariants:      name,
			StorageName:       name.appendName("_").HLCPP,
			TagName:           u.TagEnum.nestVariants(tag),
			WireOrdinalName:   u.WireOrdinalEnum.nest(tag.Wire.Name()),
			Offset:            mem.Offset,
			HandleInformation: c.fieldHandleInformation(mem.Type),
			NaturalConstraint: t.NaturalFieldConstraint,
			WireConstraint:    t.WireFieldConstraint,
			NaturalIndex:      naturalIndex,
		})
		naturalIndex++
	}

	return &u
}

func (c *compiler) compileResult(p Payloader, m *fidlgen.Method) *Result {
	valueType := c.compileType(*m.ValueType)
	result := Result{
		ResultDecl:        c.compileNameVariants(m.ResultType.Identifier),
		ValueTypeDecl:     valueType.nameVariants,
		Value:             valueType,
		HasError:          m.HasError,
		HasFrameworkError: m.HasTransportError(),
		valueTypeIsStruct: false,
	}
	if m.HasError {
		errType := c.compileType(*m.ErrorType)
		result.ErrorDecl = errType.nameVariants
		result.Error = errType
	}

	var memberTypeNames []name
	result.ValueParameters = p.AsParameters(&valueType, c.fieldHandleInformation(m.ValueType))
	for _, sm := range result.ValueParameters {
		memberTypeNames = append(memberTypeNames, sm.Type.HLCPP)
	}

	// Only struct parameters may be flattened, so check specifically for that
	// type.
	switch s := p.(type) {
	case *Struct:
		result.valueTypeIsStruct = true

		// Special case: empty structs must not generated a member type for the special "__reserved"
		// member, so erase all members in the empty case.
		if s.isEmptyStruct {
			memberTypeNames = nil
			result.ValueParameters = nil
		}
	}

	result.ValueTupleDecl = makeTupleName(memberTypeNames)
	if len(memberTypeNames) == 0 {
		result.ValueDecl = makeName("void")
	} else if len(memberTypeNames) == 1 {
		result.ValueDecl = memberTypeNames[0]
	} else {
		result.ValueDecl = result.ValueTupleDecl
	}

	c.resultForUnion[m.ResultType.Identifier] = &result

	return &result
}
