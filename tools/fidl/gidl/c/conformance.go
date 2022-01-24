// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package c

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

#include <fidl/test.conformance/cpp/wire.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/c/walker_tests/conformance_test_utils.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include "sdk/cts/tests/pkg/fidl/cpp/test/handle_util.h"
#endif

{{ range .EncodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(C_Conformance, {{ .Name }}_Encode) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	{{ .ValueBuild }}
	const auto expected_bytes = {{ .Bytes }};
	const auto expected_handles = {{ .Handles }};
	alignas(FIDL_ALIGNMENT) auto obj = {{ .ValueVar }};
	EXPECT_TRUE(c_conformance_utils::EncodeSuccess(
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
TEST(C_Conformance, {{ .Name }}_Decode) {
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
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_info_t> handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	auto equality_check = [&](void* raw_actual_value) -> bool {
		{{ .ValueType }}& {{ .Equality.InputVar }} = *static_cast<{{ .ValueType }}*>(raw_actual_value);
		{{ .Equality.HelperStatements }}
		return {{ .Equality.Expr }};
	};
	EXPECT_TRUE(c_conformance_utils::DecodeSuccess(
		{{ .WireFormatVersion }}, fidl::TypeTraits<decltype(obj)>::kType, std::move(bytes), std::move(handles), equality_check));
}

TEST(C_Conformance, {{ .Name }}_Validate) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_info_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	{{ .ValueBuild }}
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_info_t> handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(c_conformance_utils::ValidateSuccess(
		{{ .WireFormatVersion }}, fidl::TypeTraits<decltype(obj)>::kType, std::move(bytes), handles));
}
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .DecodeFailureCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(C_Conformance, {{ .Name }}_Decode_Failure) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_info_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_info_t> handles = {{ .Handles }};
	EXPECT_TRUE(c_conformance_utils::DecodeFailure({{ .WireFormatVersion }}, fidl::TypeTraits<{{ .ValueType }}>::kType, std::move(bytes), std::move(handles), {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const zx_handle_info_t handle_info : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(handle_info.handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
	}
	{{- end }}
}

TEST(C_Conformance, {{ .Name }}_Validate_Failure) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_info_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_info_t> handles = {{ .Handles }};
	EXPECT_TRUE(c_conformance_utils::ValidateFailure({{ .WireFormatVersion }}, fidl::TypeTraits<{{ .ValueType }}>::kType, std::move(bytes), handles, {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const zx_handle_info_t handle_info : handle_defs) {
		EXPECT_EQ(ZX_OK, zx_object_get_info(handle_info.handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
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
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	Name, WireFormatVersion, HandleDefs, ValueBuild, ValueVar, Bytes, Handles string
	FuchsiaOnly, CheckHandleRights                                            bool
}

type decodeSuccessCase struct {
	Name, HandleDefs, HandleKoidVectorName, ValueBuild, ValueVar, ValueType string
	Equality                                                                libllcpp.EqualityCheck
	Bytes, Handles, WireFormatVersion                                       string
	FuchsiaOnly                                                             bool
}

type decodeFailureCase struct {
	Name, HandleDefs, ValueType, Bytes, Handles, ErrorCode, WireFormatVersion string
	FuchsiaOnly                                                               bool
}

// Generate generates C tests.
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
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, schema)
	if err != nil {
		return nil, err
	}
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
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
		if containsUnionOrTable(decl) {
			continue
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
				WireFormatVersion: wireFormatName(encoding.WireFormat),
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
		if containsUnionOrTable(decl) {
			continue
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		handleDefs := libhlcpp.BuildHandleInfoDefs(decodeSuccess.HandleDefs)
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator", decodeSuccess.Value, decl, libllcpp.HandleReprInfo)
		equalityInputVar := "actual"
		handleKoidVectorName := "handle_koids"
		equality := libllcpp.BuildEqualityCheck(equalityInputVar, decodeSuccess.Value, decl, handleKoidVectorName)
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                 testCaseName(decodeSuccess.Name, encoding.WireFormat),
				HandleDefs:           handleDefs,
				HandleKoidVectorName: handleKoidVectorName,
				ValueBuild:           valueBuild,
				ValueVar:             valueVar,
				ValueType:            libllcpp.ConformanceType(gidlir.TypeFromValue(decodeSuccess.Value)),
				Equality:             equality,
				Bytes:                libhlcpp.BuildBytes(encoding.Bytes),
				Handles:              libhlcpp.BuildRawHandleInfos(encoding.Handles),
				FuchsiaOnly:          fuchsiaOnly,
				WireFormatVersion:    wireFormatName(encoding.WireFormat),
			})
		}
	}
	return decodeSuccessCases, nil
}

func decodeFailureCases(gidlDecodeFailurees []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailurees {
		decl, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		if containsUnionOrTable(decl) {
			continue
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
				HandleDefs:        handleDefs,
				ValueType:         valueType,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				Handles:           libhlcpp.BuildRawHandleInfos(encoding.Handles),
				ErrorCode:         errorCode,
				FuchsiaOnly:       fuchsiaOnly,
				WireFormatVersion: wireFormatName(encoding.WireFormat),
			})
		}
	}
	return decodeFailureCases, nil
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat || wireFormat == gidlir.V2WireFormat
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName, fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func wireFormatName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("FIDL_WIRE_FORMAT_VERSION_%s", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func containsUnionOrTable(decl gidlmixer.Declaration) bool {
	return containsUnionOrTableInternal(decl, 0)
}

func containsUnionOrTableInternal(decl gidlmixer.Declaration, depth int) bool {
	if depth > 32 {
		return false
	}
	switch decl := decl.(type) {
	case *gidlmixer.TableDecl, *gidlmixer.UnionDecl:
		return true
	case *gidlmixer.StructDecl:
		for _, fieldName := range decl.FieldNames() {
			fieldDecl, ok := decl.Field(fieldName)
			if !ok {
				panic(fmt.Sprintf("field %s not found", fieldName))
			}
			if containsUnionOrTableInternal(fieldDecl, depth+1) {
				return true
			}
		}
		return false
	case gidlmixer.ListDeclaration:
		return containsUnionOrTableInternal(decl.Elem(), depth+1)
	default:
		return false
	}
}
