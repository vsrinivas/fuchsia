// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"strconv"
	"strings"

	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func BuildValueUnowned(value gidlir.Value, decl gidlmixer.Declaration, handleRepr HandleRepr) (string, string) {
	var builder unownedBuilder
	builder.handleRepr = handleRepr
	valueVar := builder.visit(value, decl)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

type unownedBuilder struct {
	strings.Builder
	varidx     int
	handleRepr HandleRepr
}

func (b *unownedBuilder) write(format string, vals ...interface{}) {
	b.WriteString(fmt.Sprintf(format, vals...))
}

func (b *unownedBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func primitiveTypeName(subtype fidlgen.PrimitiveSubtype) string {
	switch subtype {
	case fidlgen.Bool:
		return "bool"
	case fidlgen.Uint8, fidlgen.Uint16, fidlgen.Uint32, fidlgen.Uint64,
		fidlgen.Int8, fidlgen.Int16, fidlgen.Int32, fidlgen.Int64:
		return fmt.Sprintf("%s_t", subtype)
	case fidlgen.Float32:
		return "float"
	case fidlgen.Float64:
		return "double"
	default:
		panic(fmt.Sprintf("unexpected subtype %s", subtype))
	}
}

func (b *unownedBuilder) visit(value gidlir.Value, decl gidlmixer.Declaration) string {
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
		switch decl := decl.(type) {
		case *gidlmixer.FloatDecl:
			switch decl.Subtype() {
			case fidlgen.Float32:
				s := fmt.Sprintf("%g", value)
				if strings.Contains(s, ".") {
					return fmt.Sprintf("%sf", s)
				}
				return s
			case fidlgen.Float64:
				return fmt.Sprintf("%g", value)
			}
		}
	case gidlir.RawFloat:
		switch decl.(*gidlmixer.FloatDecl).Subtype() {
		case fidlgen.Float32:
			return fmt.Sprintf("([] { uint32_t u = %#b; float f; memcpy(&f, &u, sizeof(float)); return f; })()", value)
		case fidlgen.Float64:
			return fmt.Sprintf("([] { uint64_t u = %#b; double d; memcpy(&d, &u, sizeof(double)); return d; })()", value)
		}
	case string:
		return fmt.Sprintf("fidl::StringView(%s)", strconv.Quote(value))
	case gidlir.HandleWithRights:
		if b.handleRepr == HandleReprDisposition || b.handleRepr == HandleReprInfo {
			return fmt.Sprintf("%s(handle_defs[%d].handle)", typeName(decl), value.Handle)
		}
		return fmt.Sprintf("%s(handle_defs[%d])", typeName(decl), value.Handle)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return b.visitStruct(value, decl)
		case *gidlmixer.TableDecl:
			return b.visitTable(value, decl)
		case *gidlmixer.UnionDecl:
			return b.visitUnion(value, decl)
		}
	case []gidlir.Value:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return b.visitArray(value, decl)
		case *gidlmixer.VectorDecl:
			return b.visitVector(value, decl)
		}
	case nil:
		return fmt.Sprintf("%s{}", typeName(decl))
	}
	panic(fmt.Sprintf("not implemented: %T", value))
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
		viewVar := b.newVar()
		b.write("auto %s = fidl::ObjectView<%s>::FromExternal(&%s);\n", viewVar, typeNameIgnoreNullable(decl), containerVar)
		result = viewVar
	} else {
		result = containerVar
	}
	return fmt.Sprintf("std::move(%s)", result)
}

func (b *unownedBuilder) visitTable(value gidlir.Record, decl *gidlmixer.TableDecl) string {
	frameVar := b.newVar()

	b.write(
		"%s::Frame_ %s;\n", declName(decl), frameVar)

	tableVar := b.newVar()

	b.write(
		"%s %s(::fidl::ObjectView<%s::Frame_>::FromExternal(&%s));\n", declName(decl), tableVar, declName(decl), frameVar)

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("LLCPP does not support constructing unknown fields")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldVar := b.visit(field.Value, fieldDecl)
		storageVar := b.newVar()
		b.write("auto %s = std::move(%s);\n", storageVar, fieldVar)
		if fieldDecl.IsInlinableInEnvelope() {
			b.write(
				"%s.set_%s(%s);\n", tableVar, field.Key.Name, storageVar)
		} else {
			b.write(
				"%s.set_%s(fidl::ObjectView<%s>::FromExternal(&%s));\n", tableVar, field.Key.Name, typeName(fieldDecl), storageVar)
		}
	}

	return fmt.Sprintf("std::move(%s)", tableVar)
}

func (b *unownedBuilder) visitUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) string {
	containerVar := b.newVar()

	b.write(
		"%s %s;\n", declName(decl), containerVar)

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("LLCPP does not support constructing unknown fields")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldVar := b.visit(field.Value, fieldDecl)
		storageVar := b.newVar()
		b.write("auto %s = std::move(%s);\n", storageVar, fieldVar)
		if fieldDecl.IsInlinableInEnvelope() {
			b.write(
				"%s.set_%s(%s);\n", containerVar, field.Key.Name, storageVar)

		} else {
			b.write(
				"%s.set_%s(fidl::ObjectView<%s>::FromExternal(&%s));\n", containerVar, field.Key.Name, typeName(fieldDecl), storageVar)
		}
	}
	return fmt.Sprintf("std::move(%s)", containerVar)
}

func (b *unownedBuilder) buildListItems(value []gidlir.Value, decl gidlmixer.ListDeclaration) []string {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elements = append(elements, fmt.Sprintf("%s", b.visit(item, elemDecl)))
	}
	return elements
}

func (b *unownedBuilder) visitArray(value []gidlir.Value, decl *gidlmixer.ArrayDecl) string {
	elements := b.buildListItems(value, decl)
	sliceVar := b.newVar()
	b.write("FIDL_ALIGNDECL auto %s = %s{%s};\n",
		sliceVar, typeName(decl), strings.Join(elements, ", "))
	return sliceVar
}

func (b *unownedBuilder) visitVector(value []gidlir.Value, decl *gidlmixer.VectorDecl) string {
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
	b.write("auto %s = fidl::VectorView<%s>::FromExternal(%s.data(), %d);\n", sliceVar,
		typeName(decl.Elem()), arrayVar, len(elements))
	return fmt.Sprintf("std::move(%s)", sliceVar)
}
