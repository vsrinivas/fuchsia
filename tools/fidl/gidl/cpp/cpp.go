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
	const auto expected = {{ .Bytes }};
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
	auto bytes = {{ .Bytes }};
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
	auto bytes = {{ .Bytes }};
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
	schema := gidlmixer.BuildSchema(fidl)
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, schema)
	if err != nil {
		return err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, schema)
	if err != nil {
		return err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, schema)
	if err != nil {
		return err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, schema)
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

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
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

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, schema gidlmixer.Schema) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := schema.ExtractDeclaration(decodeSuccess.Value)
		if err != nil {
			return nil, fmt.Errorf("decode success %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		var valueBuilder cppValueBuilder
		gidlmixer.Visit(&valueBuilder, decodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
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

func encodeFailureCases(gidlEncodeFailures []gidlir.EncodeFailure, schema gidlmixer.Schema) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailures {
		decl, err := schema.ExtractDeclarationUnsafe(encodeFailure.Value)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
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
			if !wireFormatSupported(wireFormat) {
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

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		_, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		valueType := cppType(decodeFailure.Type)
		errorCode := cppErrorCode(decodeFailure.Err)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
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

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat
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

func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("std::vector<uint8_t>{")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x,", b))
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("}")
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
			"%s %s = std::make_unique<conformance::%s>();\n", typeName(decl), containerVar, value.Name))
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

func identifierName(eci fidlir.EncodedCompoundIdentifier) string {
	parts := strings.Split(string(eci), "/")
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
