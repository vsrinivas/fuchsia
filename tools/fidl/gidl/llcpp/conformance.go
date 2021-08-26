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
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <conformance/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/conformance/conformance_utils.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include "sdk/cts/tests/pkg/fidl/cpp/test/handle_util.h"
#endif

{{ range .EncodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	{{ .ValueBuild }}
	const auto expected_bytes = {{ .Bytes }};
	const auto expected_handles = {{ .Handles }};
	alignas(FIDL_ALIGNMENT) auto obj = {{ .ValueVar }};
	EXPECT_TRUE(llcpp_conformance_utils::EncodeSuccess(
		{{ .WireFormatVersion }}, &obj, expected_bytes, expected_handles, {{ .CheckHandleRights }}));
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
	const std::vector<zx_handle_info_t> handle_defs = {{ .HandleDefs }};
	std::vector<zx_koid_t> {{ .HandleKoidVectorName }};
	for (zx_handle_info_t def : handle_defs) {
		zx_info_handle_basic_t info;
		ASSERT_EQ(ZX_OK, zx_object_get_info(def.handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
		{{ .HandleKoidVectorName }}.push_back(info.koid);
	}
	{{- end }}
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	{{ .ValueBuild }}
	auto bytes = {{ .Bytes }};
	auto handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	auto equality_check = [&]({{ .ValueType }}& {{ .EqualityInputVar }}) -> bool {
		{{ .EqualityBuild }}
		return {{ .EqualityValue }};
	};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeSuccess(
		{{ .WireFormatVersion }}, &obj, std::move(bytes), std::move(handles), std::move(equality_check)));
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
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
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
	const std::vector<zx_handle_info_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	auto bytes = {{ .Bytes }};
	auto handles = {{ .Handles }};
	EXPECT_TRUE(llcpp_conformance_utils::DecodeFailure<{{ .ValueType }}>(
		{{ .WireFormatVersion }}, std::move(bytes), std::move(handles), {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const auto handle_info : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(handle_info.handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
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
	Name, WireFormatVersion, HandleDefs, ValueBuild, ValueVar, Bytes, Handles string
	FuchsiaOnly, CheckHandleRights                                            bool
}

type decodeSuccessCase struct {
	Name, HandleDefs, HandleKoidVectorName         string
	WireFormatVersion                              string
	ValueBuild, ValueVar, ValueType                string
	EqualityInputVar, EqualityBuild, EqualityValue string
	Bytes, Handles                                 string
	FuchsiaOnly                                    bool
}

type encodeFailureCase struct {
	Name, HandleDefs, ValueBuild, ValueVar, ErrorCode string
	FuchsiaOnly                                       bool
}

type decodeFailureCase struct {
	Name, WireFormatVersion, HandleDefs, ValueType, Bytes, Handles, ErrorCode string
	FuchsiaOnly                                                               bool
}

// Generate generates Low-Level C++ tests.
func GenerateConformanceTests(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
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
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		handleDefs := libhlcpp.BuildHandleDefs(encodeSuccess.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator", encodeSuccess.Value, decl, libllcpp.HandleReprRaw)
		fuchsiaOnly := decl.IsResourceType() || len(encodeSuccess.HandleDefs) > 0
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:              testCaseName(encodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				HandleDefs:        handleDefs,
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				Handles:           libhlcpp.BuildRawHandleDispositions(encoding.HandleDispositions),
				FuchsiaOnly:       fuchsiaOnly,
				CheckHandleRights: encodeSuccess.CheckHandleRights,
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
		handleDefs := libhlcpp.BuildHandleInfoDefs(decodeSuccess.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator", decodeSuccess.Value, decl, libllcpp.HandleReprInfo)
		equalityInputVar := "actual"
		handleKoidVectorName := "handle_koids"
		equalityBuild, EqualityValue := libllcpp.BuildEqualityCheck(equalityInputVar, decodeSuccess.Value, decl, "handle_koids")
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                 testCaseName(decodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion:    wireFormatVersionName(encoding.WireFormat),
				HandleDefs:           handleDefs,
				ValueBuild:           valueBuild,
				ValueVar:             valueVar,
				ValueType:            libllcpp.ConformanceType(gidlir.TypeFromValue(decodeSuccess.Value)),
				EqualityInputVar:     equalityInputVar,
				EqualityBuild:        equalityBuild,
				EqualityValue:        EqualityValue,
				Bytes:                libhlcpp.BuildBytes(encoding.Bytes),
				Handles:              libhlcpp.BuildRawHandleInfos(encoding.Handles),
				FuchsiaOnly:          fuchsiaOnly,
				HandleKoidVectorName: handleKoidVectorName,
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
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator", encodeFailure.Value, decl, libllcpp.HandleReprRaw)
		errorCode := libllcpp.LlcppErrorCode(encodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(encodeFailure.HandleDefs) > 0
		for _, wireFormat := range supportedEncodeFailureFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:        testCaseName(encodeFailure.Name, wireFormat),
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
		handleDefs := libhlcpp.BuildHandleInfoDefs(decodeFailure.HandleDefs)
		valueType := libllcpp.ConformanceType(decodeFailure.Type)
		errorCode := libllcpp.LlcppErrorCode(decodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(decodeFailure.HandleDefs) > 0
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:              testCaseName(decodeFailure.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				HandleDefs:        handleDefs,
				ValueType:         valueType,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				Handles:           libhlcpp.BuildRawHandleInfos(encoding.Handles),
				ErrorCode:         errorCode,
				FuchsiaOnly:       fuchsiaOnly,
			})
		}
	}
	return decodeFailureCases, nil
}

var supportedWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
	gidlir.V2WireFormat,
}
var supportedEncodeFailureFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	for _, wf := range supportedWireFormats {
		if wireFormat == wf {
			return true
		}
	}
	return false
}

func wireFormatVersionName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("::fidl::internal::WireFormatVersion::k%s", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName, fidlgen.ToUpperCamelCase(wireFormat.String()))
}
