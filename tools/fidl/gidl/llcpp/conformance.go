// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"bytes"
	"fmt"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	libllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/llcpp/lib"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <conformance/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/test_utils.h"

{{ range .EncodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Encode) {
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	const auto expected = {{ .Bytes }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::EncodeSuccess(&obj, expected));
}
TEST(Conformance, {{ .Name }}_LinearizeAndEncode) {
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	const auto expected = {{ .Bytes }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::LinearizeAndEncodeSuccess(&obj, expected));
}
{{ end }}

{{ range .DecodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Decode) {
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	auto bytes = {{ .Bytes }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeSuccess(&obj, std::move(bytes)));
}
{{ end }}

{{ range .EncodeFailureCases }}
TEST(Conformance, {{ .Name }}_LinearizeAndEncode_Failure) {
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::LinearizeAndEncodeFailure(&obj, {{ .ErrorCode }}));
}
{{ end }}

{{ range .DecodeFailureCases }}
TEST(Conformance, {{ .Name }}_Decode_Failure) {
	auto bytes = {{ .Bytes }};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeFailure<{{ .ValueType }}>(std::move(bytes), {{ .ErrorCode }}));
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
	Name, ValueBuild, ValueVar, Bytes string
}

type decodeSuccessCase struct {
	Name, ValueBuild, ValueVar, Bytes string
}

type encodeFailureCase struct {
	Name, ValueBuild, ValueVar, ErrorCode string
}

type decodeFailureCase struct {
	Name, ValueType, Bytes, ErrorCode string
}

// Generate generates Low-Level C++ tests.
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
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
	return buf.Bytes(), nil, err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				Bytes:      libllcpp.BytesBuilder(encoding.Bytes),
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
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				Bytes:      libllcpp.BytesBuilder(encoding.Bytes),
			})
		}
	}
	return decodeSuccessCases, nil
}

func encodeFailureCases(gidlEncodeFailurees []gidlir.EncodeFailure, schema gidlmixer.Schema) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailurees {
		decl, err := schema.ExtractDeclarationUnsafe(encodeFailure.Value)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", encodeFailure.Value, decl)
		errorCode := llcppErrorCode(encodeFailure.Err)
		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:       encodeFailure.Name,
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				ErrorCode:  errorCode,
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailurees []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailurees {
		_, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		valueType := conformanceType(decodeFailure.Type)
		errorCode := llcppErrorCode(decodeFailure.Err)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:      decodeFailure.Name,
				ValueType: valueType,
				Bytes:     libllcpp.BytesBuilder(encoding.Bytes),
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
	return fmt.Sprintf("%s_%s", baseName, fidlcommon.ToUpperCamelCase(wireFormat.String()))
}

func conformanceType(gidlTypeString string) string {
	return "llcpp::conformance::" + gidlTypeString
}

func llcppErrorCode(code gidlir.ErrorCode) string {
	// TODO(fxb/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}
