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
	gidlutil "go.fuchsia.dev/fuchsia/tools/fidl/gidl/util"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func BuildValueAllocator(allocatorVar string, value gidlir.Value, decl gidlmixer.Declaration, handleRepr HandleRepr) (string, string) {
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

func (a *allocatorBuilder) adoptHandle(decl gidlmixer.Declaration, value gidlir.HandleWithRights) string {
	if a.handleRepr == HandleReprDisposition || a.handleRepr == HandleReprInfo {
		return fmt.Sprintf("%s(handle_defs[%d].handle)", typeName(decl), value.Handle)
	}
	return fmt.Sprintf("%s(handle_defs[%d])", typeName(decl), value.Handle)
}

func formatPrimitive(value gidlir.Value) string {
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

func (a *allocatorBuilder) visit(value gidlir.Value, decl gidlmixer.Declaration) string {
	// StringView and VectorView in wire types represent nullability within the object rather than as
	// as pointer to the object.
	_, isString := decl.(*gidlmixer.StringDecl)
	_, isVector := decl.(*gidlmixer.VectorDecl)
	isPointer := (decl.IsNullable() && !isString && !isVector)

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
		case fidlgen.Float32:
			return fmt.Sprintf("([] { uint32_t u = %#b; float f; memcpy(&f, &u, sizeof(float)); return f; })()", value)
		case fidlgen.Float64:
			return fmt.Sprintf("([] { uint64_t u = %#b; double d; memcpy(&d, &u, sizeof(double)); return d; })()", value)
		}
	case string:
		if !isPointer {
			// This clause is optional and simplifies the output.
			return strconv.Quote(value)
		}
		return a.construct(typeNameIgnoreNullable(decl), isPointer, "%q", value)
	case gidlir.HandleWithRights:
		switch decl := decl.(type) {
		case *gidlmixer.HandleDecl:
			return a.adoptHandle(decl, value)
		case *gidlmixer.ClientEndDecl:
			return fmt.Sprintf("%s(%s)", typeName(decl), a.adoptHandle(decl.UnderlyingHandleDecl(), value))
		case *gidlmixer.ServerEndDecl:
			return fmt.Sprintf("%s(%s)", typeName(decl), a.adoptHandle(decl.UnderlyingHandleDecl(), value))
		}
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return a.visitStruct(value, decl, isPointer)
		case *gidlmixer.TableDecl:
			return a.visitTable(value, decl, isPointer)
		case *gidlmixer.UnionDecl:
			return a.visitUnion(value, decl, isPointer)
		}
	case []gidlir.Value:
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
		if field.Key.IsUnknown() {
			panic("LLCPP does not support constructing unknown fields")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl)
		if fieldDecl.IsInlinableInEnvelope() {
			a.write("%s%sset_%s(%s);\n", t, op, fidlgen.ToSnakeCase(field.Key.Name), fieldRhs)
		} else {
			a.write("%s%sset_%s(%s, %s);\n", t, op, fidlgen.ToSnakeCase(field.Key.Name), a.allocatorVar, fieldRhs)
		}
	}

	return fmt.Sprintf("std::move(%s)", t)
}

func (a *allocatorBuilder) visitUnion(value gidlir.Record, decl *gidlmixer.UnionDecl, isPointer bool) string {
	s := a.newVar()
	unionRaw := a.assignNew(typeNameIgnoreNullable(decl), false, "")
	if isPointer {
		a.write("auto %s = fidl::WireOptional<%s>(std::move(%s));\n", s, typeNameIgnoreNullable(decl), unionRaw)
	} else {
		a.write("auto %s = std::move(%s);\n", s, unionRaw)
	}

	if len(value.Fields) == 1 {
		field := value.Fields[0]
		if field.Key.IsUnknown() {
			panic("LLCPP does not support constructing unknown fields")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl)
		if fieldDecl.IsInlinableInEnvelope() {
			a.write("%s = %s::With%s(%s);\n", s, typeNameIgnoreNullable(decl), fidlgen.ToUpperCamelCase(field.Key.Name), fieldRhs)
		} else {
			a.write("%s = %s::With%s(%s, %s);\n", s, typeNameIgnoreNullable(decl), fidlgen.ToUpperCamelCase(field.Key.Name), a.allocatorVar, fieldRhs)
		}
	}

	return fmt.Sprintf("std::move(%s)", s)
}

func (a *allocatorBuilder) visitArray(value []gidlir.Value, decl *gidlmixer.ArrayDecl, isPointer bool) string {
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

func (a *allocatorBuilder) visitVector(value []gidlir.Value, decl *gidlmixer.VectorDecl, isPointer bool) string {
	vector := a.assignNew(typeName(decl), isPointer, "%s, %d", a.allocatorVar, len(value))
	if len(value) == 0 {
		return fmt.Sprintf("std::move(%s)", vector)
	}
	// Special case unsigned integer vectors, because clang otherwise has issues with large vectors on arm.
	// This uses pattern matching so only a subset of byte vectors that fit the pattern (repeating sequence) will be optimized.
	if elemDecl, ok := decl.Elem().(gidlmixer.PrimitiveDeclaration); ok && elemDecl.Subtype() == fidlgen.Uint8 {
		var uintValues []uint64
		for _, v := range value {
			uintValues = append(uintValues, v.(uint64))
		}
		// For simplicity, only support sizes that are multiples of the period.
		if period, ok := gidlutil.FindRepeatingPeriod(uintValues); ok && len(value)%period == 0 {
			for i := 0; i < period; i++ {
				elem := a.visit(value[i], decl.Elem())
				a.write("%s[%d] = %s;\n", vector, i, elem)
			}
			a.write(
				`for (size_t offset = 0; offset < %[1]s.count(); offset += %[2]d) {
memcpy(%[1]s.data() + offset, %[1]s.data(), %[2]d);
}
`, vector, period)
			return fmt.Sprintf("std::move(%s)", vector)
		}
		if len(uintValues) > 1024 {
			panic("large vectors that are not repeating are not supported, for build performance reasons")
		}
	}
	for i, item := range value {
		elem := a.visit(item, decl.Elem())
		a.write("%s[%d] = %s;\n", vector, i, elem)
	}
	return fmt.Sprintf("std::move(%s)", vector)
}
