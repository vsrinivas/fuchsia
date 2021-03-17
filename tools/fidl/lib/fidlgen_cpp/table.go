// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type TableFrameItem *TableMember

type Table struct {
	fidl.Attributes
	fidl.Resourceness
	DeclName
	TableType      string
	Members        []TableMember
	InlineSize     int
	BiggestOrdinal int
	MaxHandles     int
	MaxOutOfLine   int
	ByteBufferType string
	HasPointer     bool

	// FrameItems stores the members in ordinal order; "null" for reserved.
	FrameItems []TableFrameItem

	// Kind should be default initialized.
	Kind tableKind
}

type TableMember struct {
	fidl.Attributes
	Type               Type
	Name               string
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
	return tm.Name, tm.Type
}

func (c *compiler) compileTableMember(val fidl.TableMember, index int) TableMember {
	t := c.compileType(val.Type)

	defaultValue := ConstantValue{}
	if val.MaybeDefaultValue != nil {
		defaultValue = c.compileConstant(*val.MaybeDefaultValue, &t, val.Type)
	}

	return TableMember{
		Attributes:         val.Attributes,
		Type:               t,
		Name:               changeIfReserved(val.Name),
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

func (c *compiler) compileTable(val fidl.Table) Table {
	name := c.compileDeclName(val.Name)
	tableType := c.compileTableType(val.Name)
	r := Table{
		Attributes:     val.Attributes,
		Resourceness:   val.Resourceness,
		DeclName:       name,
		TableType:      tableType,
		Members:        nil,
		InlineSize:     val.TypeShapeV1.InlineSize,
		BiggestOrdinal: 0,
		MaxHandles:     val.TypeShapeV1.MaxHandles,
		MaxOutOfLine:   val.TypeShapeV1.MaxOutOfLine,
		ByteBufferType: byteBufferType(val.TypeShapeV1.InlineSize, val.TypeShapeV1.MaxOutOfLine, boundednessBounded),
		HasPointer:     val.TypeShapeV1.Depth > 0,
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
