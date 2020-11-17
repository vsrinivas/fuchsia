// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"bytes"
	"fmt"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	libllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/llcpp/lib"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <conformance/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/test_utils.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include "sdk/lib/fidl/cpp/test/handle_util.h"
#endif

{{ range .EncodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	const auto expected_bytes = {{ .Bytes }};
	const auto expected_handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::EncodeSuccess(&obj, expected_bytes, expected_handles));
}
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .DecodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Decode) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	auto bytes = {{ .Bytes }};
	auto handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeSuccess(&obj, std::move(bytes), std::move(handles)));
}
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .EncodeFailureCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Encode_Failure) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	fidl::UnsafeBufferAllocator<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	fidl::Allocator* allocator_ptr __attribute__((unused)) = &allocator;
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::EncodeFailure(&obj, {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const auto handle : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
	}
	{{- end }}
}
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .DecodeFailureCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Decode_Failure) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	auto bytes = {{ .Bytes }};
	auto handles = {{ .Handles }};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeFailure<{{ .ValueType }}>(std::move(bytes), std::move(handles), {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const auto handle : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
	}
	{{- end }}
}
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}
`))

type conformanceTmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
	DecodeSuccessCases []decodeSuccessCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	Name, HandleDefs, ValueBuild, ValueVar, Bytes, Handles string
	FuchsiaOnly                                            bool
}

type decodeSuccessCase struct {
	Name, HandleDefs, ValueBuild, ValueVar, Bytes, Handles string
	FuchsiaOnly                                            bool
}

type encodeFailureCase struct {
	Name, HandleDefs, ValueBuild, ValueVar, ErrorCode string
	FuchsiaOnly                                       bool
}

type decodeFailureCase struct {
	Name, HandleDefs, ValueType, Bytes, Handles, ErrorCode string
	FuchsiaOnly                                            bool
}

// Generate generates Low-Level C++ tests.
func GenerateConformanceTests(gidl gidlir.All, fidl fidl.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, schema)
	if err != nil {
		return nil, err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, schema)
	if err != nil {
		return nil, err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, schema)
	if err != nil {
		return nil, err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, schema)
	if err != nil {
		return nil, err
	}
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
	return buf.Bytes(), err
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
		handleDefs := libhlcpp.BuildHandleDefs(encodeSuccess.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", encodeSuccess.Value, decl)
		fuchsiaOnly := decl.IsResourceType() || len(encodeSuccess.HandleDefs) > 0
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:        testCaseName(encodeSuccess.Name, encoding.WireFormat),
				HandleDefs:  handleDefs,
				ValueBuild:  valueBuild,
				ValueVar:    valueVar,
				Bytes:       libhlcpp.BuildBytes(encoding.Bytes),
				Handles:     libhlcpp.BuildRawHandles(encoding.Handles),
				FuchsiaOnly: fuchsiaOnly,
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
		handleDefs := libhlcpp.BuildHandleDefs(decodeSuccess.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", decodeSuccess.Value, decl)
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:        testCaseName(decodeSuccess.Name, encoding.WireFormat),
				HandleDefs:  handleDefs,
				ValueBuild:  valueBuild,
				ValueVar:    valueVar,
				Bytes:       libhlcpp.BuildBytes(encoding.Bytes),
				Handles:     libhlcpp.BuildRawHandles(encoding.Handles),
				FuchsiaOnly: fuchsiaOnly,
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
		handleDefs := libhlcpp.BuildHandleDefs(encodeFailure.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator_ptr", encodeFailure.Value, decl)
		errorCode := llcppErrorCode(encodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(encodeFailure.HandleDefs) > 0
		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:        encodeFailure.Name,
				HandleDefs:  handleDefs,
				ValueBuild:  valueBuild,
				ValueVar:    valueVar,
				ErrorCode:   errorCode,
				FuchsiaOnly: fuchsiaOnly,
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailurees []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailurees {
		decl, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		handleDefs := libhlcpp.BuildHandleDefs(decodeFailure.HandleDefs)
		valueType := conformanceType(decodeFailure.Type)
		errorCode := llcppErrorCode(decodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(decodeFailure.HandleDefs) > 0
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:        decodeFailure.Name,
				HandleDefs:  handleDefs,
				ValueType:   valueType,
				Bytes:       libhlcpp.BuildBytes(encoding.Bytes),
				Handles:     libhlcpp.BuildRawHandles(encoding.Handles),
				ErrorCode:   errorCode,
				FuchsiaOnly: fuchsiaOnly,
			})
		}
	}
	return decodeFailureCases, nil
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName, fidl.ToUpperCamelCase(wireFormat.String()))
}

func conformanceType(gidlTypeString string) string {
	return "llcpp::conformance::" + gidlTypeString
}

func llcppErrorCode(code gidlir.ErrorCode) string {
	// TODO(fxbug.dev/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}
