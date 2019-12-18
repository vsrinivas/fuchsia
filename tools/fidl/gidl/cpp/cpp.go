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

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpl = template.Must(template.New("tmpl").Parse(`
#include <conformance/cpp/fidl.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/test/test_util.h"

{{ range .EncodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{ .ValueBuild }}
	const auto expected = std::vector<uint8_t>{
		{{ .Bytes }}
	};
	{{/* Must use a variable because macros don't understand commas in template args. */}}
	const auto result =
		fidl::test::util::ValueToBytes<decltype({{ .ValueVar }}), {{ .EncoderType }}>(
			{{ .ValueVar }}, expected);
	EXPECT_TRUE(result);
}
{{ end }}

{{ range .DecodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Decode) {
	{{ .ValueBuild }}
	auto bytes = std::vector<uint8_t>{
		{{ .Bytes }}
	};
	EXPECT_TRUE(fidl::Equals(
		fidl::test::util::DecodedBytes<decltype({{ .ValueVar }})>(bytes),
		{{ .ValueVar }}));
}
{{ end }}

{{ range .EncodeFailureCases }}
TEST(Conformance, {{ .Name }}_Encode_Failure) {
  {{ .ValueBuild }}
  fidl::test::util::CheckEncodeFailure<decltype({{ .ValueVar }}), {{ .EncoderType }}>(
	  {{ .ValueVar }}, {{ .ErrorCode }});
}
{{ end }}

{{ range .DecodeFailureCases }}
TEST(Conformance, {{ .Name }}_Decode_Failure) {
  auto bytes = std::vector<uint8_t>{
    {{ .Bytes }}
  };
  fidl::test::util::CheckDecodeFailure<{{ .ValueType }}>(bytes, {{ .ErrorCode }});
}
{{ end }}
`))

type tmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
	DecodeSuccessCases []decodeSuccessCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	Name, EncoderType, ValueBuild, ValueVar, Bytes string
}

type decodeSuccessCase struct {
	Name, ValueBuild, ValueVar, Bytes string
}

type encodeFailureCase struct {
	Name, EncoderType, ValueBuild, ValueVar, ErrorCode string
}

type decodeFailureCase struct {
	Name, ValueType, Bytes, ErrorCode string
}

// Generate generates High-Level C++ tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, fidl)
	if err != nil {
		return err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, fidl)
	if err != nil {
		return err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, fidl)
	if err != nil {
		return err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, fidl)
	if err != nil {
		return err
	}
	input := tmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	}
	return tmpl.Execute(wr, input)
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, fidl fidlir.Root) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(encodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeSuccess %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range encodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:        testCaseName(encodeSuccess.Name, encoding.WireFormat),
				EncoderType: encoderType(encoding.WireFormat),
				ValueBuild:  valueBuild,
				ValueVar:    valueVar,
				Bytes:       bytesBuilder(encoding.Bytes),
			})
		}
	}
	return encodeSuccessCases, nil
}

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, fidl fidlir.Root) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(decodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("decodeSuccess %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, decodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range decodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				Bytes: bytesBuilder(append(
					transactionHeaderBytes(encoding.WireFormat),
					encoding.Bytes...)),
			})
		}
	}
	return decodeSuccessCases, nil
}

func encodeFailureCases(gidlEncodeFailures []gidlir.EncodeFailure, fidl fidlir.Root) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailures {
		decl, err := gidlmixer.ExtractDeclarationUnsafe(encodeFailure.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeFailure %s: %s", encodeFailure.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeFailure.Value) {
			continue
		}

		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeFailure.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		errorCode := cppErrorCode(encodeFailure.Err)
		for _, wireFormat := range encodeFailure.WireFormats {
			if wireFormat == gidlir.OldWireFormat {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:        testCaseName(encodeFailure.Name, wireFormat),
				EncoderType: encoderType(wireFormat),
				ValueBuild:  valueBuild,
				ValueVar:    valueVar,
				ErrorCode:   errorCode,
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, fidl fidlir.Root) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		valueType := cppType(decodeFailure.Type)
		errorCode := cppErrorCode(decodeFailure.Err)
		for _, encoding := range decodeFailure.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:      testCaseName(decodeFailure.Name, encoding.WireFormat),
				ValueType: valueType,
				Bytes: bytesBuilder(append(
					transactionHeaderBytes(encoding.WireFormat),
					encoding.Bytes...)),
				ErrorCode: errorCode,
			})
		}
	}
	return decodeFailureCases, nil
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName,
		fidlcommon.ToUpperCamelCase(wireFormat.String()))
}

func encoderType(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("fidl::test::util::EncoderFactory%s",
		fidlcommon.ToUpperCamelCase(wireFormat.String()))
}

func transactionHeaderBytes(wireFormat gidlir.WireFormat) []byte {
	// See the FIDL wire format spec for the transaction header layout:
	switch wireFormat {
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
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
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
