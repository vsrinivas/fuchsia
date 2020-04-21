// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"fmt"
	"strconv"
	"strings"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

func newCppValueBuilder() cppValueBuilder {
	return cppValueBuilder{}
}

type cppValueBuilder struct {
	strings.Builder

	varidx  int
	lastVar string
}

func (b *cppValueBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func (b *cppValueBuilder) OnBool(value bool) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("bool %s = %t;\n", newVar, value))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnInt64(value int64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	if value == -9223372036854775808 {
		// There are no negative integer literals in C++, so need to use arithmatic to create the minimum value.
		b.Builder.WriteString(fmt.Sprintf("%s %s = -9223372036854775807ll - 1;\n", primitiveTypeName(typ), newVar))
	} else {
		b.Builder.WriteString(fmt.Sprintf("%s %s = %dll;\n", primitiveTypeName(typ), newVar, value))
	}
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnUint64(value uint64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %dull;\n", primitiveTypeName(typ), newVar, value))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnFloat64(value float64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %g;\n", primitiveTypeName(typ), newVar, value))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnString(value string, decl *gidlmixer.StringDecl) {
	newVar := b.newVar()
	// TODO(fxb/39686) Consider Go/C++ escape sequence differences
	b.Builder.WriteString(fmt.Sprintf(
		"%s %s(%s);\n", typeName(decl), newVar, strconv.Quote(value)))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnBits(value interface{}, decl *gidlmixer.BitsDecl) {
	gidlmixer.Visit(b, value, &decl.Underlying)
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(%s);\n", newVar, typeName(decl), b.lastVar))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnEnum(value interface{}, decl *gidlmixer.EnumDecl) {
	gidlmixer.Visit(b, value, &decl.Underlying)
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(%s);\n", newVar, typeName(decl), b.lastVar))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnStruct(value gidlir.Record, decl *gidlmixer.StructDecl) {
	b.onRecord(value, decl)
}

func (b *cppValueBuilder) OnTable(value gidlir.Record, decl *gidlmixer.TableDecl) {
	b.onRecord(value, decl)
}

func (b *cppValueBuilder) OnUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) {
	b.onRecord(value, decl)
}

func (b *cppValueBuilder) onRecord(value gidlir.Record, decl gidlmixer.RecordDeclaration) {
	containerVar := b.newVar()
	nullable := decl.IsNullable()
	if nullable {
		b.Builder.WriteString(fmt.Sprintf(
			"%s %s = std::make_unique<%s>();\n", typeName(decl), containerVar, declName(decl)))
	} else {
		b.Builder.WriteString(fmt.Sprintf("%s %s;\n", typeName(decl), containerVar))
	}

	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		b.Builder.WriteString("\n")

		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		accessor := "."
		if nullable {
			accessor = "->"
		}

		switch decl.(type) {
		case *gidlmixer.StructDecl:
			b.Builder.WriteString(fmt.Sprintf(
				"%s%s%s = std::move(%s);\n", containerVar, accessor, field.Key.Name, fieldVar))
		default:
			b.Builder.WriteString(fmt.Sprintf(
				"%s%sset_%s(std::move(%s));\n", containerVar, accessor, field.Key.Name, fieldVar))
		}
	}
	b.lastVar = containerVar
}

func (b *cppValueBuilder) OnArray(value []interface{}, decl *gidlmixer.ArrayDecl) {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		elements = append(elements, fmt.Sprintf("std::move(%s)", b.lastVar))
	}
	arrayVar := b.newVar()
	// Populate the array using aggregate initialization.
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s{%s};\n",
		arrayVar, typeName(decl), strings.Join(elements, ", ")))
	b.lastVar = arrayVar
}

func (b *cppValueBuilder) OnVector(value []interface{}, decl *gidlmixer.VectorDecl) {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		elements = append(elements, b.lastVar)
	}
	vectorVar := b.newVar()
	// Populate the vector using push_back. We can't use an initializer list
	// because they always copy, which breaks if the element is a unique_ptr.
	b.Builder.WriteString(fmt.Sprintf("%s %s;\n", typeName(decl), vectorVar))
	for _, element := range elements {
		b.Builder.WriteString(fmt.Sprintf("%s.push_back(std::move(%s));\n", vectorVar, element))
	}
	b.lastVar = vectorVar
}

func (b *cppValueBuilder) OnNull(decl gidlmixer.Declaration) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s;\n", typeName(decl), newVar))
	b.lastVar = newVar
}

func typeName(decl gidlmixer.Declaration) string {
	switch decl := decl.(type) {
	case gidlmixer.PrimitiveDeclaration:
		return primitiveTypeName(decl.Subtype())
	case gidlmixer.NamedDeclaration:
		if decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", declName(decl))
		}
		return declName(decl)
	case *gidlmixer.StringDecl:
		if decl.IsNullable() {
			return "::fidl::StringPtr"
		}
		return "std::string"
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("std::array<%s, %d>", typeName(decl.Elem()), decl.Size())
	case *gidlmixer.VectorDecl:
		if decl.IsNullable() {
			return fmt.Sprintf("::fidl::VectorPtr<%s>", typeName(decl.Elem()))
		}
		return fmt.Sprintf("std::vector<%s>", typeName(decl.Elem()))
	default:
		panic("unhandled case")
	}
}

func declName(decl gidlmixer.NamedDeclaration) string {
	parts := strings.Split(decl.Name(), "/")
	return strings.Join(parts, "::")
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
