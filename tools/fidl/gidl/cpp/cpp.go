// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

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

var tmpls = template.Must(template.New("tmpls").Parse(`
{{- define "Header"}}
#include <gtest/gtest.h>

#include <lib/fidl/cpp/test/test_util.h>

#include <conformance/cpp/fidl.h>

{{end -}}

{{- define "EncodeSuccessCase"}}

TEST(Conformance, {{ .name }}_Encode) {
  {{ .value_build }}

  auto expected = std::vector<uint8_t>{
    {{ .bytes }}
  };

  EXPECT_TRUE(::fidl::test::util::ValueToBytes({{ .value_var }}, expected));
}

{{end -}}

{{- define "DecodeSuccessCase"}}

TEST(Conformance, {{ .name }}_Decode) {
  auto input = std::vector<uint8_t>{
    {{ .bytes }}
  };

  {{ .value_build }}

  auto expected = ::fidl::test::util::DecodedBytes<decltype({{ .value_var }})>(input);
  EXPECT_TRUE(::fidl::Equals({{ .value_var }}, expected));
}

{{end -}}

{{- define "EncodeFailureCase"}}

TEST(Conformance, {{ .name }}_Encode_Failure) {
  {{ .value_build }}

  zx_status_t expected = {{ .error_code }};

  ::fidl::test::util::CheckEncodeFailure({{ .value_var }}, expected);
}

{{end -}}

{{- define "DecodeFailureCase"}}

TEST(Conformance, {{ .name }}_Decode_Failure) {
  auto input = std::vector<uint8_t>{
    {{ .bytes }}
  };
  zx_status_t expected = {{ .error_code }};

  ::fidl::test::util::CheckDecodeFailure<{{ .value_type }}>(input, expected);
}

{{end -}}
`))

func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	if err := tmpls.ExecuteTemplate(wr, "Header", nil); err != nil {
		return err
	}
	// Note: while the encodeSuccess and decodeSuccess loops look identical, they operate
	// on different structures so are hard to consolidate.
	for _, encodeSuccess := range gidl.EncodeSuccess {
		decl, err := gidlmixer.ExtractDeclaration(encodeSuccess.Value, fidl)
		if err != nil {
			return fmt.Errorf("encodeSuccess %s: %s", encodeSuccess.Name, err)
		}

		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeSuccess.Value, decl)

		if err := tmpls.ExecuteTemplate(wr, "EncodeSuccessCase", map[string]interface{}{
			"name":        encodeSuccess.Name,
			"value_build": valueBuilder.String(),
			"value_var":   valueBuilder.lastVar,
			"bytes":       bytesBuilder(encodeSuccess.Bytes),
		}); err != nil {
			return err
		}
	}
	for _, decodeSuccess := range gidl.DecodeSuccess {
		decl, err := gidlmixer.ExtractDeclaration(decodeSuccess.Value, fidl)
		if err != nil {
			return fmt.Errorf("decodeSuccess %s: %s", decodeSuccess.Name, err)
		}

		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, decodeSuccess.Value, decl)

		if err := tmpls.ExecuteTemplate(wr, "DecodeSuccessCase", map[string]interface{}{
			"name":        decodeSuccess.Name,
			"value_build": valueBuilder.String(),
			"value_var":   valueBuilder.lastVar,
			"bytes": bytesBuilder(append(
				transactionHeaderBytes(decodeSuccess.WireFormat),
				decodeSuccess.Bytes...)),
		}); err != nil {
			return err
		}
	}
	for _, encodeFailure := range gidl.EncodeFailure {
		decl, err := gidlmixer.ExtractDeclarationUnsafe(encodeFailure.Value, fidl)
		if err != nil {
			return fmt.Errorf("encodeFailure %s: %s", encodeFailure.Name, err)
		}

		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeFailure.Value, decl)

		if err := tmpls.ExecuteTemplate(wr, "EncodeFailureCase", map[string]interface{}{
			"name":        encodeFailure.Name,
			"value_build": valueBuilder.String(),
			"value_var":   valueBuilder.lastVar,
			"error_code":  cppErrorCode(encodeFailure.Err),
		}); err != nil {
			return err
		}
	}
	for _, decodeFailure := range gidl.DecodeFailure {
		if err := tmpls.ExecuteTemplate(wr, "DecodeFailureCase", map[string]interface{}{
			"name":       decodeFailure.Name,
			"value_type": cppType(decodeFailure.Type),
			"bytes":      bytesBuilder(decodeFailure.Bytes),
			"error_code": cppErrorCode(decodeFailure.Err),
		}); err != nil {
			return err
		}
	}

	return nil
}

func transactionHeaderBytes(wf gidlir.WireFormat) []byte {
	// See the FIDL wire format specif for the transaction header layout.
	switch wf {
	case gidlir.V1WireFormat:
		// Flags[0] == 1 (union represented as xunion bytes)
		return []byte{
			0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		}
	case gidlir.OldWireFormat:
		return []byte{
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		}
	default:
		panic("unknown wire format")
	}
}

func cppErrorCode(code gidlir.ErrorCode) string {
	// TODO(fxb/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}

func cppType(gidlTypeString string) string {
	return "conformance::" + gidlTypeString
}

// TODO(fxb/39685) extract out to common library
func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x", b))
		builder.WriteString(", ")
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	return builder.String()
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
		b.Builder.WriteString(fmt.Sprintf("%s %s = -9223372036854775807ll - 1;\n", numberName(typ), newVar))
	} else {
		b.Builder.WriteString(fmt.Sprintf("%s %s = %dll;\n", numberName(typ), newVar, value))
	}
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnUint64(value uint64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %dull;\n", numberName(typ), newVar, value))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnFloat64(value float64, typ fidlir.PrimitiveSubtype) {
	var typename string
	switch typ {
	case fidlir.Float32:
		typename = "float"
	case fidlir.Float64:
		typename = "double"
	default:
		panic("unknown floating point type")
	}
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s %s = %g;\n", typename, newVar, value))
	b.lastVar = newVar
}

func (b *cppValueBuilder) OnString(value string, decl *gidlmixer.StringDecl) {
	newVar := b.newVar()

	// TODO(fxb/39686) Consider Go/C++ escape sequence differences
	b.Builder.WriteString(fmt.Sprintf(
		"%s %s(%s);\n", typeName(decl), newVar, strconv.Quote(value)))

	b.lastVar = newVar
}

func (b *cppValueBuilder) OnStruct(value gidlir.Object, decl *gidlmixer.StructDecl) {
	b.onObject(value, decl)
}

func (b *cppValueBuilder) OnTable(value gidlir.Object, decl *gidlmixer.TableDecl) {
	b.onObject(value, decl)
}

func (b *cppValueBuilder) OnXUnion(value gidlir.Object, decl *gidlmixer.XUnionDecl) {
	b.onObject(value, decl)
}

func (b *cppValueBuilder) OnUnion(value gidlir.Object, decl *gidlmixer.UnionDecl) {
	b.onObject(value, decl)
}

func (b *cppValueBuilder) onObject(value gidlir.Object, decl gidlmixer.KeyedDeclaration) {
	containerVar := b.newVar()
	nullable := decl.IsNullable()
	if nullable {
		b.Builder.WriteString(fmt.Sprintf(
			"%s %s = std::make_unique<conformance::%s>();\n", typeName(decl), containerVar, value.Name))
	} else {
		b.Builder.WriteString(fmt.Sprintf("%s %s;\n", typeName(decl), containerVar))
	}

	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		b.Builder.WriteString("\n")

		fieldDecl, _ := decl.ForKey(field.Key)
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
	elemDecl, _ := decl.Elem()
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
	elemDecl, _ := decl.Elem()
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
	case *gidlmixer.BoolDecl:
		return "bool"
	case *gidlmixer.NumberDecl:
		return numberName(decl.Typ)
	case *gidlmixer.FloatDecl:
		return numberName(decl.Typ)
	case *gidlmixer.StringDecl:
		if decl.IsNullable() {
			return "::fidl::StringPtr"
		}
		return "std::string"
	case *gidlmixer.StructDecl:
		if decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", identifierName(decl.Name))
		}
		return identifierName(decl.Name)
	case *gidlmixer.TableDecl:
		return identifierName(decl.Name)
	case *gidlmixer.UnionDecl:
		if decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", identifierName(decl.Name))
		}
		return identifierName(decl.Name)
	case *gidlmixer.XUnionDecl:
		if decl.IsNullable() {
			return fmt.Sprintf("std::unique_ptr<%s>", identifierName(decl.Name))
		}
		return identifierName(decl.Name)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("std::array<%s, %d>", elemName(decl), decl.Size())
	case *gidlmixer.VectorDecl:
		if decl.IsNullable() {
			return fmt.Sprintf("::fidl::VectorPtr<%s>", elemName(decl))
		}
		return fmt.Sprintf("std::vector<%s>", elemName(decl))
	default:
		panic("unhandled case")
	}
}

func identifierName(eci fidlir.EncodedCompoundIdentifier) string {
	parts := strings.Split(string(eci), "/")
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
