// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <conformance/cpp/fidl.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/test/test_util.h"

{{ range .EncodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{ .ValueBuild }}
	const auto expected = {{ .Bytes }};
	{{/* Must use a variable because macros don't understand commas in template args. */}}
	const auto result =
		fidl::test::util::ValueToBytes<{{ .ValueType }}>(
			{{ .ValueVar }}, expected);
	EXPECT_TRUE(result);
}
{{ end }}

{{ range .DecodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Decode) {
	{{ .ValueBuild }}
	auto bytes = {{ .Bytes }};
	EXPECT_TRUE(fidl::Equals(
		fidl::test::util::DecodedBytes<{{ .ValueType }}>(bytes),
		{{ .ValueVar }}));
}
{{ end }}

{{ range .EncodeFailureCases }}
TEST(Conformance, {{ .Name }}_Encode_Failure) {
	{{ .ValueBuild }}
	fidl::test::util::CheckEncodeFailure<{{ .ValueType }}>(
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

type conformanceTmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
	DecodeSuccessCases []decodeSuccessCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	Name, ValueType, ValueBuild, ValueVar, Bytes string
}

type decodeSuccessCase struct {
	Name, ValueBuild, ValueType, ValueVar, Bytes string
}

type encodeFailureCase struct {
	Name, ValueType, ValueBuild, ValueVar, ErrorCode string
}

type decodeFailureCase struct {
	Name, ValueType, Bytes, ErrorCode string
}

// Generate generates High-Level C++ tests.
func GenerateConformanceTests(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, schema)
	if err != nil {
		return nil, nil, err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, schema)
	if err != nil {
		return nil, nil, err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, schema)
	if err != nil {
		return nil, nil, err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, schema)
	if err != nil {
		return nil, nil, err
	}
	input := conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	}
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(encodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				ValueType:  declName(decl),
				Bytes:      bytesBuilder(encoding.Bytes),
			})
		}
	}
	return encodeSuccessCases, nil
}

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, schema gidlmixer.Schema) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := schema.ExtractDeclaration(decodeSuccess.Value, decodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("decode success %s: %s", decodeSuccess.Name, err)
		}
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(decodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				ValueType:  declName(decl),
				Bytes:      bytesBuilder(encoding.Bytes),
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

		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(encodeFailure.Value, decl)
		valueBuild := valueBuilder.String()
		errorCode := cppErrorCode(encodeFailure.Err)
		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:       testCaseName(encodeFailure.Name, wireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				ValueType:  declName(decl),
				ErrorCode:  errorCode,
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
		valueType := cppConformanceType(decodeFailure.Type)
		errorCode := cppErrorCode(decodeFailure.Err)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:      testCaseName(decodeFailure.Name, encoding.WireFormat),
				ValueType: valueType,
				Bytes:     bytesBuilder(encoding.Bytes),
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

func cppErrorCode(code gidlir.ErrorCode) string {
	// TODO(fxbug.dev/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}

func cppConformanceType(gidlTypeString string) string {
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
