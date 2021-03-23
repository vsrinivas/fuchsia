// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Union struct {
	fidlgen.Attributes
	fidlgen.Strictness
	fidlgen.Resourceness
	DeclName
	CodingTableType string
	Members         []UnionMember
	InlineSize      int
	MaxHandles      int
	MaxOutOfLine    int
	Result          *Result
	HasPointer      bool
}

func (Union) Kind() declKind {
	return Kinds.Union
}

var _ Kinded = (*Union)(nil)

type UnionMember struct {
	fidlgen.Attributes
	Ordinal           uint64
	Type              Type
	Name              string
	StorageName       string
	TagName           string
	Offset            int
	HandleInformation *HandleInformation
}

func (um UnionMember) UpperCamelCaseName() string {
	return fidlgen.ToUpperCamelCase(um.Name)
}

func (um UnionMember) NameAndType() (string, Type) {
	return um.Name, um.Type
}

func (c *compiler) compileUnionMember(val fidlgen.UnionMember) UnionMember {
	n := changeIfReserved(val.Name)
	return UnionMember{
		Attributes:        val.Attributes,
		Ordinal:           uint64(val.Ordinal),
		Type:              c.compileType(val.Type),
		Name:              n,
		StorageName:       changeIfReserved(val.Name + "_"),
		TagName:           fmt.Sprintf("k%s", fidlgen.ToUpperCamelCase(n)),
		Offset:            val.Offset,
		HandleInformation: c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileUnion(val fidlgen.Union) Union {
	name := c.compileDeclName(val.Name)
	codingTableType := c.compileCodingTableType(val.Name)
	r := Union{
		Attributes:      val.Attributes,
		Strictness:      val.Strictness,
		Resourceness:    val.Resourceness,
		DeclName:        name,
		CodingTableType: codingTableType,
		InlineSize:      val.TypeShapeV1.InlineSize,
		MaxHandles:      val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:    val.TypeShapeV1.MaxOutOfLine,
		HasPointer:      val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.Members {
		if v.Reserved {
			continue
		}
		r.Members = append(r.Members, c.compileUnionMember(v))
	}

	if val.MethodResult != nil {
		result := Result{
			ResultDecl:      r.DeclName,
			ValueStructDecl: r.Members[0].Type.TypeName,
			ErrorDecl:       r.Members[1].Type.TypeName,
		}
		c.resultForStruct[val.MethodResult.ValueType.Identifier] = &result
		c.resultForUnion[val.Name] = &result
		r.Result = &result
	}

	return r
}
