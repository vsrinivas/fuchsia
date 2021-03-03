// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"strconv"
	"strings"

	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func BuildValueAllocator(allocatorVar string, value interface{}, decl gidlmixer.Declaration, handleRepr HandleRepr) (string, string) {
	var builder allocatorBuilder
	builder.allocatorVar = allocatorVar
	builder.handleRepr = handleRepr
	valueVar := builder.visit(value, decl)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

type allocatorBuilder struct {
	allocatorVar string
	strings.Builder
	varidx     int
	handleRepr HandleRepr
}

func (a *allocatorBuilder) write(format string, vals ...interface{}) {
	a.WriteString(fmt.Sprintf(format, vals...))
}

func (a *allocatorBuilder) newVar() string {
	a.varidx++
	return fmt.Sprintf("var%d", a.varidx)
}

func (a *allocatorBuilder) assignNew(typename string, isPointer bool, fmtStr string, vals ...interface{}) string {
	rhs := a.construct(typename, isPointer, fmtStr, vals...)
	newVar := a.newVar()
	a.write("auto %s = %s;\n", newVar, rhs)
	return newVar
}

func (a *allocatorBuilder) construct(typename string, isPointer bool, fmtStr string, args ...interface{}) string {
	val := fmt.Sprintf(fmtStr, args...)
	if !isPointer {
		return fmt.Sprintf("%s(%s)", typename, val)
	}
	return fmt.Sprintf("fidl::ObjectView<%s>(%s, %s)", typename, a.allocatorVar, val)
}

func formatPrimitive(value interface{}) string {
	switch value := value.(type) {
	case int64:
		if value == -9223372036854775808 {
			return "-9223372036854775807ll - 1"
		}
		return fmt.Sprintf("%dll", value)
	case uint64:
		return fmt.Sprintf("%dull", value)
	case float64:
		return fmt.Sprintf("%g", value)
	}
	panic("Unreachable")
}

func (a *allocatorBuilder) visit(value interface{}, decl gidlmixer.Declaration) string {
	// Unions, StringView and VectorView in LLCPP represent nullability within the object rather than as
	// as pointer to the object.
	_, isUnion := decl.(*gidlmixer.UnionDecl)
	_, isString := decl.(*gidlmixer.StringDecl)
	_, isVector := decl.(*gidlmixer.VectorDecl)
	isPointer := (decl.IsNullable() && !isUnion && !isString && !isVector)

	switch value := value.(type) {
	case bool:
		return a.construct(typeName(decl), isPointer, "%t", value)
	case int64, uint64, float64:
		switch decl := decl.(type) {
		case gidlmixer.PrimitiveDeclaration, *gidlmixer.EnumDecl:
			return a.construct(typeName(decl), isPointer, formatPrimitive(value))
		case *gidlmixer.BitsDecl:
			return fmt.Sprintf("static_cast<%s>(%s)", declName(decl), formatPrimitive(value))
		}
	case gidlir.RawFloat:
		switch decl.(*gidlmixer.FloatDecl).Subtype() {
		case fidl.Float32:
			return fmt.Sprintf("([] { uint32_t u = %#b; float f; memcpy(&f, &u, 4); return f; })()", value)
		case fidl.Float64:
			return fmt.Sprintf("([] { uint64_t u = %#b; double d; memcpy(&d, &u, 8); return d; })()", value)
		}
	case string:
		if !isPointer {
			// This clause is optional and simplifies the output.
			return strconv.Quote(value)
		}
		return a.construct(typeNameIgnoreNullable(decl), isPointer, "%q", value)
	case gidlir.HandleWithRights:
		if a.handleRepr == HandleReprDisposition || a.handleRepr == HandleReprInfo {
			return fmt.Sprintf("%s(handle_defs[%d].handle)", typeName(decl), value.Handle)
		}
		return fmt.Sprintf("%s(handle_defs[%d])", typeName(decl), value.Handle)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return a.visitStruct(value, decl, isPointer)
		case *gidlmixer.TableDecl:
			return a.visitTable(value, decl, isPointer)
		case *gidlmixer.UnionDecl:
			return a.visitUnion(value, decl, isPointer)
		}
	case []interface{}:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return a.visitArray(value, decl, isPointer)
		case *gidlmixer.VectorDecl:
			return a.visitVector(value, decl, isPointer)
		}
	case nil:
		return a.construct(typeName(decl), false, "")
	}
	panic(fmt.Sprintf("not implemented: %T", value))
}

func (a *allocatorBuilder) visitStruct(value gidlir.Record, decl *gidlmixer.StructDecl, isPointer bool) string {
	s := a.newVar()
	structRaw := fmt.Sprintf("%s{}", typeNameIgnoreNullable(decl))
	var op string
	if isPointer {
		op = "->"
		a.write("auto %s = fidl::ObjectView<%s>(%s, %s);\n", s, typeNameIgnoreNullable(decl), a.allocatorVar, structRaw)
	} else {
		op = "."
		a.write("auto %s = %s;\n", s, structRaw)
	}

	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl)
		a.write("%s%s%s = %s;\n", s, op, field.Key.Name, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", s)
}

func (a *allocatorBuilder) visitTable(value gidlir.Record, decl *gidlmixer.TableDecl, isPointer bool) string {
	t := a.assignNew(declName(decl), isPointer, "%s", a.allocatorVar)
	op := "."
	if isPointer {
		op = "->"
	}

	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl)
		a.write("%s%sset_%s(%s, %s);\n", t, op, field.Key.Name, a.allocatorVar, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", t)
}

func (a *allocatorBuilder) visitUnion(value gidlir.Record, decl *gidlmixer.UnionDecl, isPointer bool) string {
	union := a.assignNew(typeNameIgnoreNullable(decl), isPointer, "")
	op := "."
	if isPointer {
		op = "->"
	}

	if len(value.Fields) == 1 {
		field := value.Fields[0]
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl)
		a.write("%s%sset_%s(%s, %s);\n", union, op, field.Key.Name, a.allocatorVar, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", union)
}

func (a *allocatorBuilder) visitArray(value []interface{}, decl *gidlmixer.ArrayDecl, isPointer bool) string {
	array := a.assignNew(typeNameIgnoreNullable(decl), isPointer, "")
	op := ""
	if isPointer {
		op = ".get()"
	}
	for i, item := range value {
		elem := a.visit(item, decl.Elem())
		a.write("%s%s[%d] = %s;\n", array, op, i, elem)
	}
	return fmt.Sprintf("std::move(%s)", array)
}

func (a *allocatorBuilder) visitVector(value []interface{}, decl *gidlmixer.VectorDecl, isPointer bool) string {
	vector := a.assignNew(typeName(decl), isPointer, "%s, %d", a.allocatorVar, len(value))
	for i, item := range value {
		elem := a.visit(item, decl.Elem())
		a.write("%s[%d] = %s;\n", vector, i, elem)
	}
	return fmt.Sprintf("std::move(%s)", vector)
}
