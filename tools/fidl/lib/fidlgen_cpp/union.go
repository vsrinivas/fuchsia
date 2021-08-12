// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Union struct {
	Attributes
	fidlgen.Strictness
	fidlgen.Resourceness
	nameVariants
	CodingTableType    string
	AnonymousChildren  []ScopedLayout
	TagEnum            nameVariants
	TagUnknown         nameVariants
	TagInvalid         nameVariants
	WireOrdinalEnum    name
	WireInvalidOrdinal name
	Members            []UnionMember
	Result             *Result
	TypeShapeV1        TypeShape
	TypeShapeV2        TypeShape
}

func (Union) Kind() declKind {
	return Kinds.Union
}

var _ Kinded = (*Union)(nil)
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
}

func (um UnionMember) UpperCamelCaseName() string {
	return fidlgen.ToUpperCamelCase(um.Name())
}

func (um UnionMember) NameAndType() (string, Type) {
	return um.Name(), um.Type
}
func (c *compiler) compileUnion(val fidlgen.Union) Union {
	name := c.compileNameVariants(val.Name)
	codingTableType := c.compileCodingTableType(val.Name)
	tagEnum := name.nest("Tag")
	wireOrdinalEnum := name.Wire.nest("Ordinal")
	u := Union{
		Attributes:         Attributes{val.Attributes},
		TypeShapeV1:        TypeShape{val.TypeShapeV1},
		TypeShapeV2:        TypeShape{val.TypeShapeV2},
		AnonymousChildren:  c.getAnonymousChildren(val.Layout),
		Strictness:         val.Strictness,
		Resourceness:       val.Resourceness,
		nameVariants:       name,
		CodingTableType:    codingTableType,
		TagEnum:            tagEnum,
		TagUnknown:         tagEnum.nest("kUnknown"),
		TagInvalid:         tagEnum.nest("Invalid"),
		WireOrdinalEnum:    wireOrdinalEnum,
		WireInvalidOrdinal: wireOrdinalEnum.nest("Invalid"),
	}

	for _, mem := range val.Members {
		if mem.Reserved {
			continue
		}
		name := unionMemberContext.transform(mem.Name)
		tag := unionMemberTagContext.transform(mem.Name)
		u.Members = append(u.Members, UnionMember{
			Attributes:        Attributes{mem.Attributes},
			Ordinal:           uint64(mem.Ordinal),
			Type:              c.compileType(mem.Type),
			nameVariants:      name,
			StorageName:       name.appendName("_").Natural,
			TagName:           u.TagEnum.nestVariants(tag),
			WireOrdinalName:   u.WireOrdinalEnum.nest(tag.Wire.Name()),
			Offset:            mem.Offset,
			HandleInformation: c.fieldHandleInformation(&mem.Type),
		})
	}

	if val.MethodResult != nil {
		result := Result{
			ResultDecl:      u.nameVariants,
			ValueStructDecl: u.Members[0].Type.nameVariants,
			ErrorDecl:       u.Members[1].Type.nameVariants,
		}
		c.resultForStruct[val.MethodResult.ValueType.Identifier] = &result
		c.resultForUnion[val.Name] = &result
		u.Result = &result
	}

	return u
}
