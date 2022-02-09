// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"fmt"
	"strings"

	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	gidlutil "go.fuchsia.dev/fuchsia/tools/fidl/gidl/util"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type handleRepr int

const (
	_ = iota
	handleReprDisposition
	handleReprInfo
	handleReprRaw
)

func buildValue(value gidlir.Value, decl gidlmixer.Declaration, handleRepr handleRepr) (string, string) {
	builder := builder{
		handleRepr: handleRepr,
	}
	valueVar := builder.visit(value, decl)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

type builder struct {
	strings.Builder
	varidx     int
	handleRepr handleRepr
}

func (b *builder) write(format string, vals ...interface{}) {
	b.WriteString(fmt.Sprintf(format, vals...))
}

func (b *builder) newVar() string {
	b.varidx++
	return fmt.Sprintf("var%d", b.varidx)
}

func (b *builder) assignNew(typename string, isPointer bool, fmtStr string, vals ...interface{}) string {
	rhs := b.construct(typename, isPointer, fmtStr, vals...)
	newVar := b.newVar()
	b.write("auto %s = %s;\n", newVar, rhs)
	return newVar
}

func (b *builder) construct(typename string, isPointer bool, fmtStr string, args ...interface{}) string {
	val := fmt.Sprintf(fmtStr, args...)
	if !isPointer {
		return fmt.Sprintf("%s(%s)", typename, val)
	}
	return fmt.Sprintf("std::make_unique<%s>(%s)", typename, val)
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
	panic(fmt.Sprintf("unknown primitive type %T", value))
}

func (a *builder) visit(value gidlir.Value, decl gidlmixer.Declaration) string {
	// std::optional is used to represent nullability for strings and vectors.
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
		return a.construct(typeNameIgnoreNullable(decl), isPointer, "%q, %d", value, len(value))
	case gidlir.HandleWithRights:
		if a.handleRepr == handleReprDisposition || a.handleRepr == handleReprInfo {
			return fmt.Sprintf("%s(handle_defs[%d].handle)", typeName(decl), value.Handle)
		}
		return fmt.Sprintf("%s(handle_defs[%d])", typeName(decl), value.Handle)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return a.visitStructOrTable(value, decl, isPointer)
		case *gidlmixer.TableDecl:
			return a.visitStructOrTable(value, decl, isPointer)
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

func (b *builder) visitStructOrTable(value gidlir.Record, decl gidlmixer.RecordDeclaration, isPointer bool) string {
	s := b.newVar()
	structRaw := fmt.Sprintf("%s{}", typeNameIgnoreNullable(decl))
	var op string
	if isPointer {
		op = "->"
		b.write("auto %s = std::make_unique<%s>(%s);\n", s, typeNameIgnoreNullable(decl), structRaw)
	} else {
		op = "."
		b.write("auto %s = %s;\n", s, structRaw)
	}

	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := b.visit(field.Value, fieldDecl)
		b.write("%s%s%s() = %s;\n", s, op, field.Key.Name, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", s)
}

func (a *builder) visitUnion(value gidlir.Record, decl *gidlmixer.UnionDecl, isPointer bool) string {
	if len(value.Fields) == 0 {
		return a.assignNew(typeNameIgnoreNullable(decl), isPointer, "")
	}

	field := value.Fields[0]
	if field.Key.IsUnknown() {
		panic("GIDL natural type backend does not support constructing unknown fields")
	}
	fieldDecl, ok := decl.Field(field.Key.Name)
	if !ok {
		panic(fmt.Sprintf("field %s not found", field.Key.Name))
	}
	fieldRhs := a.visit(field.Value, fieldDecl)

	varName := a.assignNew(typeNameIgnoreNullable(decl), isPointer, "%s::With%s(%s)", typeNameIgnoreNullable(decl), fidlgen.ToUpperCamelCase(field.Key.Name), fieldRhs)
	return fmt.Sprintf("std::move(%s)", varName)
}

func (a *builder) visitArray(value []gidlir.Value, decl *gidlmixer.ArrayDecl, isPointer bool) string {
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

func (a *builder) visitVector(value []gidlir.Value, decl *gidlmixer.VectorDecl, isPointer bool) string {
	vector := a.assignNew(typeName(decl), isPointer, "%d", len(value))
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
				`for (size_t offset = 0; offset < %[1]s.size(); offset += %[2]d) {
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
		if decl.IsNullable() {
			a.write("%s.value()[%d] = %s;\n", vector, i, elem)
		} else {
			a.write("%s[%d] = %s;\n", vector, i, elem)
		}
	}
	return fmt.Sprintf("std::move(%s)", vector)
}

func typeNameImpl(decl gidlmixer.Declaration, ignoreNullable bool) string {
	switch decl := decl.(type) {
	case gidlmixer.PrimitiveDeclaration:
		return primitiveTypeName(decl.Subtype())
	case *gidlmixer.StringDecl:
		if !ignoreNullable && decl.IsNullable() {
			return "std::optional<std::string>"
		}
		return "std::string"
	case *gidlmixer.StructDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", declName(decl))
		}
		return declName(decl)
	case *gidlmixer.UnionDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", declName(decl))
		}
		return declName(decl)
	case gidlmixer.NamedDeclaration:
		return declName(decl)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("std::array<%s, %d>", typeName(decl.Elem()), decl.Size())
	case *gidlmixer.VectorDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("std::optional<std::vector<%s>>", typeName(decl.Elem()))
		}
		return fmt.Sprintf("std::vector<%s>", typeName(decl.Elem()))
	case *gidlmixer.HandleDecl:
		switch decl.Subtype() {
		case fidlgen.Handle:
			return "zx::handle"
		case fidlgen.Channel:
			return "zx::channel"
		case fidlgen.Event:
			return "zx::event"
		default:
			panic(fmt.Sprintf("Handle subtype not supported %s", decl.Subtype()))
		}
	default:
		panic("unhandled case")
	}
}

func typeName(decl gidlmixer.Declaration) string {
	return typeNameImpl(decl, false)
}

func typeNameIgnoreNullable(decl gidlmixer.Declaration) string {
	return typeNameImpl(decl, true)
}

func declName(decl gidlmixer.NamedDeclaration) string {
	// Note: only works for domain objects (not protocols & services)
	parts := strings.SplitN(decl.Name(), "/", 2)
	return fmt.Sprintf("%s::%s", strings.ReplaceAll(parts[0], ".", "_"), fidlgen.ToUpperCamelCase(parts[1]))
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
