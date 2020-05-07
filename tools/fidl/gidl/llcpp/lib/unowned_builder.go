// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	fidlir "fidl/compiler/backend/types"
	"fmt"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
	"strconv"
	"strings"
)

func BuildValueUnowned(value interface{}, decl gidlmixer.Declaration) (string, string) {
	var builder llcppValueBuilder
	gidlmixer.Visit(&builder, value, decl)
	valueBuild := builder.String()
	valueVar := builder.lastVar
	return valueBuild, valueVar
}

type llcppValueBuilder struct {
	strings.Builder
	varidx  int
	lastVar string
}

func (b *llcppValueBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func (b *llcppValueBuilder) OnBool(value bool) {
	newVar := b.newVar()
	// FIDL_ALIGNDECL is needed to avoid tracking_ptrs that don't have an LSB of 0.
	b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL bool %s = %t;\n", newVar, value))
	b.lastVar = newVar
}

func primitiveTypeName(subtype fidlir.PrimitiveSubtype) string {
	switch subtype {
	case fidlir.Bool:
		return "bool"
	case fidlir.Uint8, fidlir.Uint16, fidlir.Uint32, fidlir.Uint64,
		fidlir.Int8, fidlir.Int16, fidlir.Int32, fidlir.Int64:
		return fmt.Sprintf("%s_t", subtype)
	case fidlir.Float32:
		return "float"
	case fidlir.Float64:
		return "double"
	default:
		panic(fmt.Sprintf("unexpected subtype %s", subtype))
	}
}

func (b *llcppValueBuilder) OnInt64(value int64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	if value == -9223372036854775808 {
		// There are no negative integer literals in C++, so need to use arithmetic to create the minimum value.
		// FIDL_ALIGNDECL is needed to avoid tracking_ptrs that don't have an LSB of 0.
		b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL %s %s = -9223372036854775807ll - 1;\n", primitiveTypeName(typ), newVar))
	} else {
		// FIDL_ALIGNDECL is needed to avoid tracking_ptrs that don't have an LSB of 0.
		b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL %s %s = %dll;\n", primitiveTypeName(typ), newVar, value))
	}
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnUint64(value uint64, subtype fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	// FIDL_ALIGNDECL is needed to avoid tracking_ptrs that don't have an LSB of 0.
	b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL %s %s = %dull;\n", primitiveTypeName(subtype), newVar, value))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnFloat64(value float64, subtype fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	// FIDL_ALIGNDECL is needed to avoid tracking_ptrs that don't have an LSB of 0.
	b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL %s %s = %g;\n", primitiveTypeName(subtype), newVar, value))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnString(value string, decl *gidlmixer.StringDecl) {
	newVar := b.newVar()
	// TODO(fxb/39686) Consider Go/C++ escape sequence differences
	b.Builder.WriteString(fmt.Sprintf(
		"fidl::StringView %s(%s, %d)\n;", newVar, strconv.Quote(value), len(value)))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnBits(value interface{}, decl *gidlmixer.BitsDecl) {
	gidlmixer.Visit(b, value, &decl.Underlying)
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(%s);\n", newVar, typeName(decl), b.lastVar))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnEnum(value interface{}, decl *gidlmixer.EnumDecl) {
	gidlmixer.Visit(b, value, &decl.Underlying)
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(%s);\n", newVar, typeName(decl), b.lastVar))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnStruct(value gidlir.Record, decl *gidlmixer.StructDecl) {
	containerVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"%s %s{};\n", declName(decl), containerVar))
	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		gidlmixer.Visit(b, field.Value, fieldDecl)
		b.Builder.WriteString(fmt.Sprintf(
			"%s.%s = std::move(%s);\n", containerVar, field.Key.Name, b.lastVar))
	}
	if decl.IsNullable() {
		alignedVar := b.newVar()
		b.Builder.WriteString(fmt.Sprintf("fidl::aligned<%s> %s = std::move(%s);\n", typeNameIgnoreNullable(decl), alignedVar, containerVar))
		unownedVar := b.newVar()
		b.Builder.WriteString(fmt.Sprintf("%s %s = fidl::unowned_ptr(&%s);\n", typeName(decl), unownedVar, alignedVar))
		b.lastVar = unownedVar
	} else {
		b.lastVar = containerVar
	}
}

func (b *llcppValueBuilder) OnTable(value gidlir.Record, decl *gidlmixer.TableDecl) {
	frameVar := b.newVar()

	b.Builder.WriteString(fmt.Sprintf(
		"auto %s = %s::Frame();\n", frameVar, declName(decl)))

	builderVar := b.newVar()

	b.Builder.WriteString(fmt.Sprintf(
		"auto %s = %s::Builder(fidl::unowned_ptr(&%s));\n", builderVar, declName(decl), frameVar))

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar
		alignedVar := b.newVar()
		b.Builder.WriteString(fmt.Sprintf("fidl::aligned<%s> %s = std::move(%s);\n", typeName(fieldDecl), alignedVar, fieldVar))
		b.Builder.WriteString(fmt.Sprintf(
			"%s.set_%s(fidl::unowned_ptr(&%s));\n", builderVar, field.Key.Name, alignedVar))

	}
	tableVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"auto %s = %s.build();\n", tableVar, builderVar))

	b.lastVar = tableVar
}

func (b *llcppValueBuilder) OnUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) {
	containerVar := b.newVar()

	b.Builder.WriteString(fmt.Sprintf(
		"%s %s;\n", declName(decl), containerVar))

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar
		alignedVar := b.newVar()
		b.Builder.WriteString(fmt.Sprintf("fidl::aligned<%s> %s = std::move(%s);\n", typeName(fieldDecl), alignedVar, fieldVar))
		b.Builder.WriteString(fmt.Sprintf(
			"%s.set_%s(fidl::unowned_ptr(&%s));\n", containerVar, field.Key.Name, alignedVar))
	}
	b.lastVar = containerVar
}

func (b *llcppValueBuilder) OnArray(value []interface{}, decl *gidlmixer.ArrayDecl) {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		elements = append(elements, fmt.Sprintf("std::move(%s)", b.lastVar))
	}
	sliceVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("FIDL_ALIGNDECL auto %s = %s{%s};\n",
		sliceVar, typeName(decl), strings.Join(elements, ", ")))
	b.lastVar = sliceVar
}

func (b *llcppValueBuilder) OnVector(value []interface{}, decl *gidlmixer.VectorDecl) {
	if len(value) == 0 {
		sliceVar := b.newVar()
		b.Builder.WriteString(fmt.Sprintf("auto %s = %s();\n",
			sliceVar, typeName(decl)))
		b.lastVar = sliceVar
		return

	}
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		elements = append(elements, fmt.Sprintf("std::move(%s)", b.lastVar))
	}
	arrayVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = fidl::Array<%s, %d>{%s};\n",
		arrayVar, typeName(elemDecl), len(elements), strings.Join(elements, ", ")))
	sliceVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(fidl::unowned_ptr(%s.data()), %d);\n",
		sliceVar, typeName(decl), arrayVar, len(elements)))
	b.lastVar = sliceVar
}

func (b *llcppValueBuilder) OnNull(decl gidlmixer.Declaration) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s{};\n", typeName(decl), newVar))
	b.lastVar = newVar
}
