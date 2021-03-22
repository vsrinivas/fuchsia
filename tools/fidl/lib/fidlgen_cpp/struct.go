// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"sort"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Struct struct {
	fidl.Attributes
	fidl.Resourceness
	DeclName
	CodingTableType string
	Members         []StructMember
	InlineSize      int
	MaxHandles      int
	MaxOutOfLine    int
	ByteBufferType  string
	HasPadding      bool
	IsResultValue   bool
	HasPointer      bool
	Result          *Result
	// Full decls needed to check if a type is memcpy compatible.
	// Only set if it may be possible for a type to be memcpy compatible,
	// e.g. has no padding.
	// See the struct template for usage.
	FullDeclMemcpyCompatibleDeps []string
}

func (Struct) Kind() declKind {
	return Kinds.Struct
}

var _ Kinded = (*Struct)(nil)

type StructMember struct {
	fidl.Attributes
	Type              Type
	Name              string
	DefaultValue      ConstantValue
	Offset            int
	HandleInformation *HandleInformation
}

func (m StructMember) AsParameter() Parameter {
	return Parameter{
		Type:              m.Type,
		Name:              m.Name,
		Offset:            m.Offset,
		HandleInformation: m.HandleInformation,
	}
}

func (sm StructMember) NameAndType() (string, Type) {
	return sm.Name, sm.Type
}

func (c *compiler) compileStructMember(val fidl.StructMember) StructMember {
	t := c.compileType(val.Type)

	defaultValue := ConstantValue{}
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type)
	}

	return StructMember{
		Attributes:        val.Attributes,
		Type:              t,
		Name:              changeIfReserved(val.Name),
		DefaultValue:      defaultValue,
		Offset:            val.FieldShapeV1.Offset,
		HandleInformation: c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileStruct(val fidl.Struct) Struct {
	name := c.compileDeclName(val.Name)
	codingTableType := c.compileCodingTableType(val.Name)
	r := Struct{
		Attributes:      val.Attributes,
		Resourceness:    val.Resourceness,
		DeclName:        name,
		CodingTableType: codingTableType,
		Members:         []StructMember{},
		InlineSize:      val.TypeShapeV1.InlineSize,
		MaxHandles:      val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:    val.TypeShapeV1.MaxOutOfLine,
		ByteBufferType: computeAllocation(
			val.TypeShapeV1.InlineSize, val.TypeShapeV1.MaxOutOfLine, boundednessBounded).
			ByteBufferType(),
		HasPadding: val.TypeShapeV1.HasPadding,
		HasPointer: val.TypeShapeV1.Depth > 0,
	}

	for _, v := range val.Members {
		r.Members = append(r.Members, c.compileStructMember(v))
	}

	result := c.resultForStruct[val.Name]
	if result != nil {
		memberTypeDecls := []string{}
		for _, m := range r.Members {
			memberTypeDecls = append(memberTypeDecls, string(m.Type.Natural))
			result.ValueMembers = append(result.ValueMembers, m.AsParameter())
		}
		result.ValueTupleDecl = TypeVariant(fmt.Sprintf("::std::tuple<%s>", strings.Join(memberTypeDecls, ", ")))

		if len(r.Members) == 0 {
			result.ValueDecl = TypeVariant("void")
		} else if len(r.Members) == 1 {
			result.ValueDecl = r.Members[0].Type.Natural
		} else {
			result.ValueDecl = result.ValueTupleDecl
		}

		r.IsResultValue = true
		r.Result = result
	}

	if len(r.Members) == 0 {
		r.Members = []StructMember{
			c.compileStructMember(fidl.EmptyStructMember("__reserved")),
		}
	}

	// Construct a deduped list of decls for IsMemcpyCompatible template definitions.
	memcpyCompatibleDepMap := make(map[string]struct{})
	for _, member := range r.Members {
		// The dangerous identifiers test package contains identifiers that won't compile.
		// e.g. ::fidl::test::dangerous::struct::types::camel::Interface gives an
		// "expected unqualified-id" error because of "struct".
		// There isn't an easily accessible dangerous identifiers list to replace identifiers.
		if strings.Contains(string(member.Type.Natural), "::fidl::test::dangerous::") {
			memcpyCompatibleDepMap = nil
			break
		}
		memcpyCompatibleDepMap[string(member.Type.Natural)] = struct{}{}
	}
	for decl := range memcpyCompatibleDepMap {
		r.FullDeclMemcpyCompatibleDeps = append(r.FullDeclMemcpyCompatibleDeps, decl)
	}
	sort.Strings(r.FullDeclMemcpyCompatibleDeps)

	return r
}
