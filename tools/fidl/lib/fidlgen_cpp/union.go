// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Union struct {
	fidl.Attributes
	fidl.Strictness
	fidl.Resourceness
	DeclName
	CodingTableType string
	Members         []UnionMember
	InlineSize      int
	MaxHandles      int
	MaxOutOfLine    int
	Result          *Result
	HasPointer      bool

	// Kind should be default initialized.
	Kind unionKind
}

type UnionMember struct {
	fidl.Attributes
	Ordinal           uint64
	Type              Type
	Name              string
	StorageName       string
	TagName           string
	Offset            int
	HandleInformation *HandleInformation
}

func (um UnionMember) UpperCamelCaseName() string {
	return fidl.ToUpperCamelCase(um.Name)
}

func (um UnionMember) NameAndType() (string, Type) {
	return um.Name, um.Type
}

func (c *compiler) compileUnionMember(val fidl.UnionMember) UnionMember {
	n := changeIfReserved(val.Name)
	return UnionMember{
		Attributes:        val.Attributes,
		Ordinal:           uint64(val.Ordinal),
		Type:              c.compileType(val.Type),
		Name:              n,
		StorageName:       changeIfReserved(val.Name + "_"),
		TagName:           fmt.Sprintf("k%s", fidl.ToUpperCamelCase(n)),
		Offset:            val.Offset,
		HandleInformation: c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileUnion(val fidl.Union) Union {
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

	// TODO(yifeit): Refer to fxrev.dev/502839 and replace this with equivalent logic in fidlgen.
	if val.Attributes.HasAttribute("Result") {
		if len(r.Members) != 2 {
			panic(fmt.Sprintf("result union must have two members: %v", val.Name))
		}
		if val.Members[0].Type.Kind != fidl.IdentifierType {
			panic(fmt.Sprintf("value member of result union must be an identifier: %v", val.Name))
		}
		valueStructDeclInfo, ok := c.decls[val.Members[0].Type.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Members[0].Type.Identifier))
		}
		if valueStructDeclInfo.Type != "struct" {
			panic(fmt.Sprintf("first member of result union not a struct: %v", val.Name))
		}
		result := Result{
			ResultDecl:      r.DeclName,
			ValueStructDecl: r.Members[0].Type.TypeName,
			ErrorDecl:       r.Members[1].Type.TypeName,
		}
		c.resultForStruct[val.Members[0].Type.Identifier] = &result
		c.resultForUnion[val.Name] = &result
		r.Result = &result
	}

	return r
}
