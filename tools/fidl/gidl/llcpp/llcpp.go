// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpl = template.Must(template.New("tmpl").Parse(`
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <conformance/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "garnet/public/lib/fidl/llcpp/test_utils.h"

{{ range .EncodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Encode) {
	const auto expected = std::vector<uint8_t>{
		{{ .Bytes }}
	};

	{
		{{ .ValueBuild }}

		EXPECT_TRUE(llcpp_conformance_utils::EncodeSuccess({{ .ValueVarName }}, expected));
	}
}
{{ end }}

{{ range .DecodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Decode) {
	const auto expected = std::vector<uint8_t>{
		{{ .Bytes }}
	};

	{
		{{ .ValueBuild }}

		EXPECT_TRUE(llcpp_conformance_utils::DecodeSuccess({{ .ValueVarName }}, expected));
	}
}
{{ end }}
`))

type tmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
	DecodeSuccessCases []decodeSuccessCase
}

type encodeSuccessCase struct {
	Name, ValueBuild, ValueVarName, Bytes string
}

type decodeSuccessCase struct {
	Name, ValueBuild, ValueVarName, Bytes string
}

// Generate generates Low-Level C++ tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, fidl)
	if err != nil {
		return err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, fidl)
	if err != nil {
		return err
	}
	return tmpl.Execute(wr, tmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
	})
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, fidl fidlir.Root) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(encodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		var valueBuilder llcppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeSuccess.Value, decl)
		encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
			Name:         encodeSuccess.Name,
			ValueBuild:   valueBuilder.String(),
			ValueVarName: valueBuilder.lastVar,
			Bytes:        bytesBuilder(encodeSuccess.Bytes),
		})
	}
	return encodeSuccessCases, nil
}

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, fidl fidlir.Root) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(decodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("decode success %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		var valueBuilder llcppValueBuilder
		gidlmixer.Visit(&valueBuilder, decodeSuccess.Value, decl)
		decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
			Name:         decodeSuccess.Name,
			ValueBuild:   valueBuilder.String(),
			ValueVarName: valueBuilder.lastVar,
			Bytes:        bytesBuilder(decodeSuccess.Bytes),
		})
	}
	return decodeSuccessCases, nil
}

// extract out to common library (this is the same code as golang.go)
func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x", b))
		builder.WriteString(",")
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	return builder.String()
}

type llcppValueBuilder struct {
	strings.Builder
	varidx int

	context    gidlmixer.Declaration
	contextKey gidlir.FieldKey

	lastVar string
}

func (b *llcppValueBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func (b *llcppValueBuilder) OnBool(value bool) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("bool %s = %t;\n", newVar, value))
	b.lastVar = newVar
}

func integerTypeName(subtype fidlir.PrimitiveSubtype) string {
	switch subtype {
	case "int8":
		return "int8_t"
	case "uint8":
		return "uint8_t"
	case "int16":
		return "int16_t"
	case "uint16":
		return "uint16_t"
	case "int32":
		return "int32_t"
	case "uint32":
		return "uint32_t"
	case "int64":
		return "int64_t"
	case "uint64":
		return "uint64_t"
	default:
		panic(fmt.Sprintf("Unexpected subtype %s", string(subtype)))
	}
}

func (b *llcppValueBuilder) OnInt64(value int64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	if value == -9223372036854775808 {
		// There are no negative integer literals in C++, so need to use arithmetic to create the minimum value.
		b.Builder.WriteString(fmt.Sprintf("%s %s = -9223372036854775807ll - 1;\n", integerTypeName(typ), newVar))
	} else {
		b.Builder.WriteString(fmt.Sprintf("%s %s = %dll;\n", integerTypeName(typ), newVar, value))
	}
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnUint64(value uint64, subtype fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %dull;\n", integerTypeName(subtype), newVar, value))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnFloat64(value float64, subtype fidlir.PrimitiveSubtype) {
	var typename string
	switch subtype {
	case fidlir.Float32:
		typename = "float"
	case fidlir.Float64:
		typename = "double"
	default:
		panic("unknown floating point type")
	}
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %f;\n", typename, newVar, value))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnString(value string) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"fidl::StringView %s(%s, %d)\n;", newVar, strconv.Quote(value), len(value)))
	b.lastVar = newVar
}

func (b *llcppValueBuilder) OnStruct(value gidlir.Object, decl *gidlmixer.StructDecl) {
	containerVar := b.newVar()

	in_heap := false
	switch context := b.context.(type) {
	case *gidlmixer.StructDecl:
		in_heap = context.IsKeyNullable(b.contextKey)
	}

	if in_heap {
		b.Builder.WriteString(fmt.Sprintf(
			"llcpp::conformance::%s %s_pointee{};\n", value.Name, containerVar))
		b.Builder.WriteString(fmt.Sprintf(
			"auto %s = &%s_pointee;\n", containerVar, containerVar))
	} else {

		bufName := b.newVar()
		b.Builder.WriteString(
			fmt.Sprintf("char buf_%s[llcpp_conformance_utils::FidlAlign(sizeof(llcpp::conformance::%s))];\n",
				bufName, value.Name))
		b.Builder.WriteString(
			fmt.Sprintf("llcpp::conformance::%s* %s = new (buf_%s) llcpp::conformance::%s();\n",
				value.Name, containerVar, bufName, value.Name))
	}

	b.context = decl

	for _, field := range value.Fields {
		b.contextKey = field.Key
		typ, _ := decl.MemberType(field.Key)
		fieldDecl, _ := decl.ForKey(field.Key)
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		var rhs string
		if _, ok := fieldDecl.(*gidlmixer.StructDecl); ok {
			if typ.Nullable {
				rhs = fieldVar
			} else {
				rhs = fmt.Sprintf("std::move(*%s)", fieldVar)
			}
		} else if _, isXUnion := fieldDecl.(*gidlmixer.XUnionDecl); typ.Nullable && !isXUnion {
			rhs = fmt.Sprintf("&%s", fieldVar)
		} else {
			rhs = fmt.Sprintf("std::move(%s)", fieldVar)
		}

		b.Builder.WriteString(fmt.Sprintf(
			"%s->%s = %s;\n", containerVar, field.Key.Name, rhs))
	}
	b.lastVar = containerVar
}

func (b *llcppValueBuilder) declIsNullable() bool {
	switch context := b.context.(type) {
	case *gidlmixer.StructDecl:
		return context.IsKeyNullable(b.contextKey)
	default:
		return false
	}
}

func (b *llcppValueBuilder) OnTable(value gidlir.Object, decl *gidlmixer.TableDecl) {
	builderVar := b.newVar()

	b.Builder.WriteString(fmt.Sprintf(
		"auto %s = llcpp::conformance::%s::Build();\n", builderVar, value.Name))

	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		fieldDecl, _ := decl.ForKey(field.Key)
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		if _, ok := fieldDecl.(*gidlmixer.StructDecl); ok {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(%s);\n", builderVar, field.Key.Name, fieldVar))
		} else {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(&%s);\n", builderVar, field.Key.Name, fieldVar))
		}
	}
	tableVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"auto %s = %s.view();\n", tableVar, builderVar))

	b.lastVar = tableVar
}

func (b *llcppValueBuilder) OnXUnion(value gidlir.Object, decl *gidlmixer.XUnionDecl) {
	containerVar := b.newVar()

	b.Builder.WriteString(fmt.Sprintf(
		"llcpp::conformance::%s %s;\n", value.Name, containerVar))

	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		fieldDecl, _ := decl.ForKey(field.Key)
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		if _, ok := fieldDecl.(*gidlmixer.StructDecl); ok {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(%s);\n", containerVar, field.Key.Name, fieldVar))
		} else {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(&%s);\n", containerVar, field.Key.Name, fieldVar))
		}
	}
	b.lastVar = containerVar
}

func (b *llcppValueBuilder) OnUnion(value gidlir.Object, decl *gidlmixer.UnionDecl) {
	containerVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"llcpp::conformance::%s %s;\n", value.Name, containerVar))
	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		fieldDecl, _ := decl.ForKey(field.Key)
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		if _, ok := fieldDecl.(*gidlmixer.StructDecl); ok {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(*%s);\n", containerVar, field.Key.Name, fieldVar))
		} else {
			b.Builder.WriteString(fmt.Sprintf(
				"%s.set_%s(&%s);\n", containerVar, field.Key.Name, fieldVar))
		}
	}
	b.lastVar = containerVar
}

func (b *llcppValueBuilder) OnArray(value []interface{}, decl *gidlmixer.ArrayDecl) {
	var elements []string
	elemDecl, _ := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		if _, ok := elemDecl.(*gidlmixer.StructDecl); ok {
			elements = append(elements, "*"+b.lastVar)
		} else {
			elements = append(elements, b.lastVar)
		}
	}
	sliceVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s{%s};\n",
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
	elemDecl, _ := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		if _, ok := elemDecl.(*gidlmixer.StructDecl); ok {
			elements = append(elements, "*"+b.lastVar)
		} else {
			elements = append(elements, b.lastVar)
		}
	}
	arrayVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = fidl::Array<%s, %d>{%s};\n",
		arrayVar, elemName(decl), len(elements), strings.Join(elements, ", ")))
	sliceVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("auto %s = %s(%s.data(), %d);\n",
		sliceVar, typeName(decl), arrayVar, len(elements)))
	b.lastVar = sliceVar
}

func typeName(decl gidlmixer.Declaration) string {
	switch decl := decl.(type) {
	case *gidlmixer.BoolDecl:
		return "bool"
	case *gidlmixer.NumberDecl:
		return numberName(decl.Typ)
	case *gidlmixer.StringDecl:
		return "fidl::StringView"
	case *gidlmixer.StructDecl:
		return identifierName(decl.Name)
	case *gidlmixer.TableDecl:
		return identifierName(decl.Name)
	case *gidlmixer.UnionDecl:
		return identifierName(decl.Name)
	case *gidlmixer.XUnionDecl:
		return identifierName(decl.Name)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("fidl::Array<%s, %d>", elemName(decl), decl.Size())
	case *gidlmixer.VectorDecl:
		return fmt.Sprintf("fidl::VectorView<%s>", elemName(decl))
	default:
		panic("unhandled case")
	}
}

func identifierName(eci fidlir.EncodedCompoundIdentifier) string {
	parts := strings.Split(string(eci), "/")
	if parts[0] == "conformance" {
		parts = append([]string{"llcpp"}, parts...)
	}
	return strings.Join(parts, "::")
}

func elemName(parent gidlmixer.ListDeclaration) string {
	if elemDecl, ok := parent.Elem(); ok {
		return typeName(elemDecl)
	}
	panic("missing element")
}

func numberName(primitiveSubtype fidlir.PrimitiveSubtype) string {
	return fmt.Sprintf("%s_t", primitiveSubtype)
}
