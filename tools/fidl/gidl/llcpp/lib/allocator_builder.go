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
)

// Builds a LLCPP object using std::make_unique.
func BuildValueHeap(value interface{}, decl gidlmixer.Declaration) (string, string) {
	var builder allocatorBuilder
	builder.allocationFunc = "std::make_unique"
	valueVar := builder.visit(value, decl, false)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

// Builds an LLCPP object using fidl::Allocator.
func BuildValueAllocator(allocatorVar string, value interface{}, decl gidlmixer.Declaration) (string, string) {
	var builder allocatorBuilder
	builder.allocationFunc = fmt.Sprintf("%s->make", allocatorVar)
	valueVar := builder.visit(value, decl, false)
	valueBuild := builder.String()
	return valueBuild, valueVar
}

type allocatorBuilder struct {
	allocationFunc string
	strings.Builder
	varidx int
}

func (a *allocatorBuilder) write(format string, vals ...interface{}) {
	a.WriteString(fmt.Sprintf(format, vals...))
}

func (a *allocatorBuilder) newVar() string {
	a.varidx++
	return fmt.Sprintf("var%d", a.varidx)
}

func (a *allocatorBuilder) assignNew(typename string, isPointer bool, str string, vals ...interface{}) string {
	rhs := a.construct(typename, isPointer, str, vals...)
	newVar := a.newVar()
	a.write("auto %s = %s;\n", newVar, rhs)
	return newVar
}

func (a *allocatorBuilder) construct(typename string, isPointer bool, str string, args ...interface{}) string {
	if isPointer {
		return fmt.Sprintf("%s<%s>(%s)", a.allocationFunc, typename, fmt.Sprintf(str, args...))
	}
	return fmt.Sprintf("%s(%s)", typename, fmt.Sprintf(str, args...))
}

func (a *allocatorBuilder) visit(value interface{}, decl gidlmixer.Declaration, isAlwaysPointer bool) string {
	// Unions, StringView and VectorView in LLCPP represent nullability within the object rather than as
	// as pointer to the object.
	_, isUnion := decl.(*gidlmixer.UnionDecl)
	_, isString := decl.(*gidlmixer.StringDecl)
	_, isVector := decl.(*gidlmixer.VectorDecl)
	isPointer := (decl.IsNullable() && !isUnion && !isString && !isVector) || isAlwaysPointer

	switch value := value.(type) {
	case bool:
		return a.construct(typeName(decl), isPointer, "%t", value)
	case int64:
		if value == -9223372036854775808 {
			return a.construct(typeName(decl), isPointer, "-9223372036854775807ll - 1")
		}
		return a.construct(typeName(decl), isPointer, "%dll", value)
	case uint64:
		return a.construct(typeName(decl), isPointer, "%dull", value)
	case float64:
		return a.construct(typeName(decl), isPointer, "%f", value)
	case string:
		if !isPointer {
			// This clause is optional and simplifies the output.
			return strconv.Quote(value)
		}
		return a.construct(typeNameIgnoreNullable(decl), isPointer, "%q", value)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return a.visitStruct(value, decl, isPointer)
		case *gidlmixer.TableDecl:
			return a.visitTable(value, decl, isPointer)
		case *gidlmixer.UnionDecl:
			return a.visitUnion(value, decl, isPointer)
		default:
			panic("unknown record decl type")
		}
	case []interface{}:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return a.visitArray(value, decl, isPointer)
		case *gidlmixer.VectorDecl:
			return a.visitVector(value, decl, isPointer)
		default:
			panic("unknown list decl type")
		}
	case nil:
		return a.construct(typeName(decl), false, "")
	default:
		panic(fmt.Sprintf("%T not implemented", value))
	}
}

func (a *allocatorBuilder) visitStruct(value gidlir.Record, decl *gidlmixer.StructDecl, isPointer bool) string {
	s := a.assignNew(typeNameIgnoreNullable(decl), isPointer, "")
	op := "."
	if isPointer {
		op = "->"
	}

	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl, false)
		a.write("%s%s%s = %s;\n", s, op, field.Key.Name, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", s)
}

func (a *allocatorBuilder) visitTable(value gidlir.Record, decl *gidlmixer.TableDecl, isPointer bool) string {
	frame := a.construct(fmt.Sprintf("%s::Frame", declName(decl)), true, "")
	t := a.assignNew(fmt.Sprintf("%s::Builder", declName(decl)), isPointer, "%s", frame)
	op := "."
	if isPointer {
		op = "->"
	}

	for _, field := range value.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldRhs := a.visit(field.Value, fieldDecl, true)
		a.write("%s%sset_%s(%s);\n", t, op, field.Key.Name, fieldRhs)
	}

	if isPointer {
		return a.construct(typeName(decl), true, "%s->build()", t)
	}
	return fmt.Sprintf("%s.build()", t)
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
		fieldRhs := a.visit(field.Value, fieldDecl, true)
		a.write("%s%sset_%s(%s);\n", union, op, field.Key.Name, fieldRhs)
	}

	return fmt.Sprintf("std::move(%s)", union)
}

func (a *allocatorBuilder) visitArray(value []interface{}, decl *gidlmixer.ArrayDecl, isPointer bool) string {
	var elemList []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elemList = append(elemList, a.visit(item, elemDecl, false))
	}
	elems := strings.Join(elemList, ", ")
	if isPointer {
		return fmt.Sprintf("%s<%s>(%s{%s})", a.allocationFunc, typeNameIgnoreNullable(decl), typeNameIgnoreNullable(decl), elems)
	}
	return fmt.Sprintf("%s{%s}", typeName(decl), elems)
}

func (a *allocatorBuilder) visitVector(value []interface{}, decl *gidlmixer.VectorDecl, isPointer bool) string {
	elemDecl := decl.Elem()
	array := a.assignNew(fmt.Sprintf("%s[]", typeName(elemDecl)), true, "%d", len(value))
	for i, item := range value {
		elem := a.visit(item, elemDecl, false)
		a.write("%s[%d] = %s;\n", array, i, elem)
	}
	return a.construct(typeName(decl), isPointer, "std::move(%s), %d", array, len(value))
}
