// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"strconv"
	"strings"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

func BuildValueUnowned(value interface{}, decl gidlmixer.Declaration) (string, string) {
	var builder unownedBuilder
	valueVar := builder.visit(value, decl)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

type unownedBuilder struct {
	strings.Builder
	varidx int
}

func (b *unownedBuilder) write(format string, vals ...interface{}) {
	b.WriteString(fmt.Sprintf(format, vals...))
}

func (b *unownedBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
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

func (b *unownedBuilder) visit(value interface{}, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool:
		return fmt.Sprintf("%t", value)
	case uint64:
		return fmt.Sprintf("%s(%dll)", typeName(decl), value)
	case int64:
		if value == -9223372036854775808 {
			return fmt.Sprintf("%s(-9223372036854775807ll - 1)", typeName(decl))
		}
		return fmt.Sprintf("%s(%dll)", typeName(decl), value)
	case float64:
		return fmt.Sprintf("%g", value)
	case string:
		return fmt.Sprintf("fidl::StringView(%s, %d)", strconv.Quote(value), len(value))
	case gidlir.Handle:
		return fmt.Sprintf("%s(handle_defs[%d])", typeName(decl), value)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return b.visitStruct(value, decl)
		case *gidlmixer.TableDecl:
			return b.visitTable(value, decl)
		case *gidlmixer.UnionDecl:
			return b.visitUnion(value, decl)
		default:
			panic("unknown record decl type")
		}
	case []interface{}:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return b.visitArray(value, decl)
		case *gidlmixer.VectorDecl:
			return b.visitVector(value, decl)
		default:
			panic("unknown list decl type")
		}
	case nil:
		return fmt.Sprintf("%s{}", typeName(decl))
	default:
		panic(fmt.Sprintf("%T not implemented", value))
	}
}

func (b *unownedBuilder) visitStruct(value gidlir.Record, decl *gidlmixer.StructDecl) string {
	containerVar := b.newVar()
	b.write(
		"%s %s{};\n", declName(decl), containerVar)
	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}

		stringBeforeVisitField := b.String()
		fieldValue := b.visit(field.Value, fieldDecl)
		// if visiting the field does not write any data to the string builder
		// then its return value is a temporary object and so cannot be moved
		// (which will prevent copy elision)
		if stringBeforeVisitField == b.String() {
			b.write("%s.%s = %s;\n", containerVar, field.Key.Name, fieldValue)
		} else {
			b.write("%s.%s = std::move(%s);\n", containerVar, field.Key.Name, fieldValue)
		}
	}
	var result string
	if decl.IsNullable() {
		alignedVar := b.newVar()
		b.write("fidl::aligned<%s> %s = std::move(%s);\n", typeNameIgnoreNullable(decl), alignedVar, containerVar)
		unownedVar := b.newVar()
		b.write("%s %s = fidl::unowned_ptr(&%s);\n", typeName(decl), unownedVar, alignedVar)
		result = unownedVar
	} else {
		result = containerVar
	}
	return fmt.Sprintf("std::move(%s)", result)
}

func (b *unownedBuilder) visitTable(value gidlir.Record, decl *gidlmixer.TableDecl) string {
	frameVar := b.newVar()

	b.write(
		"auto %s = %s::Frame();\n", frameVar, declName(decl))

	builderVar := b.newVar()

	b.write(
		"auto %s = %s::Builder(fidl::unowned_ptr(&%s));\n", builderVar, declName(decl), frameVar)

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldVar := b.visit(field.Value, fieldDecl)
		alignedVar := b.newVar()
		b.write("fidl::aligned<%s> %s = std::move(%s);\n", typeName(fieldDecl), alignedVar, fieldVar)
		b.write(
			"%s.set_%s(fidl::unowned_ptr(&%s));\n", builderVar, field.Key.Name, alignedVar)

	}
	tableVar := b.newVar()
	b.write(
		"auto %s = %s.build();\n", tableVar, builderVar)

	return fmt.Sprintf("std::move(%s)", tableVar)
}

func (b *unownedBuilder) visitUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) string {
	containerVar := b.newVar()

	b.write(
		"%s %s;\n", declName(decl), containerVar)

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldVar := b.visit(field.Value, fieldDecl)
		alignedVar := b.newVar()
		b.write("fidl::aligned<%s> %s = %s;\n", typeName(fieldDecl), alignedVar, fieldVar)
		b.write(
			"%s.set_%s(fidl::unowned_ptr(&%s));\n", containerVar, field.Key.Name, alignedVar)
	}
	return fmt.Sprintf("std::move(%s)", containerVar)
}

func (b *unownedBuilder) buildListItems(value []interface{}, decl gidlmixer.ListDeclaration) []string {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elements = append(elements, fmt.Sprintf("%s", b.visit(item, elemDecl)))
	}
	return elements
}

func (b *unownedBuilder) visitArray(value []interface{}, decl *gidlmixer.ArrayDecl) string {
	elements := b.buildListItems(value, decl)
	sliceVar := b.newVar()
	b.write("FIDL_ALIGNDECL auto %s = %s{%s};\n",
		sliceVar, typeName(decl), strings.Join(elements, ", "))
	return sliceVar
}

func (b *unownedBuilder) visitVector(value []interface{}, decl *gidlmixer.VectorDecl) string {
	if len(value) == 0 {
		sliceVar := b.newVar()
		b.write("auto %s = %s();\n",
			sliceVar, typeName(decl))
		return sliceVar
	}
	elements := b.buildListItems(value, decl)
	arrayVar := b.newVar()
	b.write("auto %s = fidl::Array<%s, %d>{%s};\n",
		arrayVar, typeName(decl.Elem()), len(elements), strings.Join(elements, ", "))
	sliceVar := b.newVar()
	b.write("auto %s = %s(fidl::unowned_ptr(%s.data()), %d);\n",
		sliceVar, typeName(decl), arrayVar, len(elements))
	return fmt.Sprintf("std::move(%s)", sliceVar)
}
