// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type TableFrameItem *TableMember

type Table struct {
	Attributes
	fidlgen.Resourceness
	nameVariants
	CodingTableType   string
	Members           []TableMember
	InlineSize        int
	BiggestOrdinal    int
	MaxHandles        int
	MaxOutOfLine      int
	BackingBufferType string
	HasPointer        bool

	// FrameItems stores the members in ordinal order; "null" for reserved.
	FrameItems []TableFrameItem
}

func (Table) Kind() declKind {
	return Kinds.Table
}

var _ Kinded = (*Table)(nil)
var _ namespaced = (*Table)(nil)

type TableMember struct {
	Attributes
	nameVariants
	Type               Type
	DefaultValue       ConstantValue
	Ordinal            int
	FieldPresenceIsSet string
	FieldPresenceSet   string
	FieldPresenceClear string
	FieldDataName      string
	MethodHasName      string
	MethodClearName    string
	ValueUnionName     string
	HandleInformation  *HandleInformation
}

func (tm TableMember) NameAndType() (string, Type) {
	return tm.Name(), tm.Type
}

func (c *compiler) compileTableMember(val fidlgen.TableMember, index int) TableMember {
	t := c.compileType(val.Type)

	defaultValue := ConstantValue{}
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type)
	}

	return TableMember{
		Attributes:         Attributes{val.Attributes},
		nameVariants:       tableMemberContext.transform(val.Name),
		Type:               t,
		DefaultValue:       defaultValue,
		Ordinal:            val.Ordinal,
		FieldPresenceIsSet: fmt.Sprintf("field_presence_.IsSet<%d>()", val.Ordinal-1),
		FieldPresenceSet:   fmt.Sprintf("field_presence_.Set<%d>()", val.Ordinal-1),
		FieldPresenceClear: fmt.Sprintf("field_presence_.Clear<%d>()", val.Ordinal-1),
		FieldDataName:      fmt.Sprintf("%s_value_", val.Name),
		MethodHasName:      fmt.Sprintf("has_%s", val.Name),
		MethodClearName:    fmt.Sprintf("clear_%s", val.Name),
		ValueUnionName:     fmt.Sprintf("ValueUnion_%s", val.Name),
		HandleInformation:  c.fieldHandleInformation(&val.Type),
	}
}

func (c *compiler) compileTable(val fidlgen.Table) Table {
	name := c.compileNameVariants(val.Name)
	codingTableType := c.compileCodingTableType(val.Name)
	r := Table{
		Attributes:      Attributes{val.Attributes},
		Resourceness:    val.Resourceness,
		nameVariants:    name,
		CodingTableType: codingTableType,
		Members:         nil,
		InlineSize:      val.TypeShapeV1.InlineSize,
		BiggestOrdinal:  0,
		MaxHandles:      val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:    val.TypeShapeV1.MaxOutOfLine,
		BackingBufferType: computeAllocation(
			val.TypeShapeV1.InlineSize, val.TypeShapeV1.MaxOutOfLine, boundednessBounded).
			BackingBufferType(),
		HasPointer: val.TypeShapeV1.Depth > 0,
	}

	for i, v := range val.SortedMembersNoReserved() {
		m := c.compileTableMember(v, i)
		if m.Ordinal > r.BiggestOrdinal {
			r.BiggestOrdinal = m.Ordinal
		}
		r.Members = append(r.Members, m)
	}

	r.FrameItems = make([]TableFrameItem, r.BiggestOrdinal)
	for index, member := range r.Members {
		r.FrameItems[member.Ordinal-1] = &r.Members[index]
	}

	return r
}
