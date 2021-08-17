// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hlcpp

import (
	"bytes"
	"fmt"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <zxtest/zxtest.h>

#include <conformance/cpp/natural_types.h>
#include <cts/tests/pkg/fidl/cpp/test/test_util.h>

#ifdef __Fuchsia__
#include <cts/tests/pkg/fidl/cpp/test/handle_util.h>
#include <zircon/syscalls.h>
#endif

{{ range .EncodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{- if .HandleDefs }}
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	{{ .ValueBuild }}
	const auto expected_bytes = {{ .Bytes }};
	const auto expected_handles = {{ .Handles }};
	{{/* Must use a variable because macros don't understand commas in template args. */}}
	const auto result =
		fidl::test::util::ValueToBytes<{{ .ValueType }}>(
			{{ .EncoderWireFormat }}, {{ .ValueVar }}, expected_bytes, expected_handles, {{ .CheckRights }});
	EXPECT_TRUE(result);
	{{- /* The handles are closed by the fidl::Message destructor in ValueToBytes. */}}
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
	const auto handle_defs = {{ .HandleDefs }};
	std::vector<zx_koid_t> {{ .HandleKoidVectorName }};
	for (zx_handle_info_t def : handle_defs) {
		zx_info_handle_basic_t info;
		ASSERT_OK(zx_object_get_info(def.handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
		{{ .HandleKoidVectorName }}.push_back(info.koid);
	}
	{{- end }}
	auto bytes = {{ .Bytes }};
	// TODO(fxbug.dev/81889) Remove the following line once the transformer is no longer needed.
	bytes.reserve(ZX_CHANNEL_MAX_MSG_BYTES); // For transforming V2 -> V1
	auto handles = {{ .Handles }};
	auto {{ .ActualValueVar }} =
	    fidl::test::util::DecodedBytes<{{ .ValueType }}>({{ .WireFormatHeader }}, std::move(bytes), std::move(handles));
	{{ .EqualityCheck }}
	{{- /* The handles are closed in the destructor of .ValueVar */}}
	fidl::test::util::ForgetHandles({{ .EncoderWireFormat }}, std::move(value));
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
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	{{ .ValueBuild }}
	fidl::test::util::CheckEncodeFailure<{{ .ValueType }}>(
		{{ .EncoderWireFormat }}, {{ .ValueVar }}, {{ .ErrorCode }});
	{{- if .HandleDefs }}
	for (const auto handle_def : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(
			handle_def, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
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
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	auto bytes = {{ .Bytes }};
	// TODO(fxbug.dev/81889) Remove the following line once the transformer is no longer needed.
	bytes.reserve(ZX_CHANNEL_MAX_MSG_BYTES); // For transforming V2 -> V1
	auto handles = {{ .Handles }};
	fidl::test::util::CheckDecodeFailure<{{ .ValueType }}>({{ .WireFormatHeader }}, std::move(bytes), std::move(handles), {{ .ErrorCode }});
	{{- if .HandleDefs }}
	for (const auto handle_def : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(
			handle_def.handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
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
	Name, HandleDefs, ValueType, ValueBuild, ValueVar, Bytes, Handles, EncoderWireFormat string
	FuchsiaOnly, CheckRights                                                             bool
}

type decodeSuccessCase struct {
	Name, HandleDefs, ValueType, ActualValueVar, EqualityCheck, Bytes, Handles, HandleKoidVectorName, WireFormatHeader, EncoderWireFormat string
	FuchsiaOnly                                                                                                                           bool
}

type encodeFailureCase struct {
	Name, HandleDefs, ValueType, ValueBuild, ValueVar, ErrorCode, EncoderWireFormat string
	FuchsiaOnly                                                                     bool
}

type decodeFailureCase struct {
	Name, HandleDefs, ValueType, Bytes, Handles, ErrorCode, WireFormatHeader string
	FuchsiaOnly                                                              bool
}

// Generate generates High-Level C++ tests.
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
	input := conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	}
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, input)
	return buf.Bytes(), err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		handleDefs := BuildHandleDefs(encodeSuccess.HandleDefs)
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(encodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		fuchsiaOnly := decl.IsResourceType() || len(encodeSuccess.HandleDefs) > 0
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:              testCaseName(encodeSuccess.Name, encoding.WireFormat),
				HandleDefs:        handleDefs,
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				ValueType:         declName(decl),
				Bytes:             BuildBytes(encoding.Bytes),
				Handles:           BuildRawHandleDispositions(encoding.HandleDispositions),
				FuchsiaOnly:       fuchsiaOnly,
				CheckRights:       encodeSuccess.CheckHandleRights,
				EncoderWireFormat: encoderWireFormat(encoding.WireFormat),
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
		handleDefs := BuildHandleInfoDefs(decodeSuccess.HandleDefs)
		actualValueVar := "value"
		handleKoidVectorName := "handle_koids"
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		equalityCheck := BuildEqualityCheck(actualValueVar, decodeSuccess.Value, decl, handleKoidVectorName)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                 testCaseName(decodeSuccess.Name, encoding.WireFormat),
				HandleDefs:           handleDefs,
				ActualValueVar:       actualValueVar,
				ValueType:            declName(decl),
				Bytes:                BuildBytes(encoding.Bytes),
				Handles:              BuildRawHandleInfos(encoding.Handles),
				FuchsiaOnly:          fuchsiaOnly,
				EqualityCheck:        equalityCheck,
				HandleKoidVectorName: handleKoidVectorName,
				WireFormatHeader:     wireFormatHeader(encoding.WireFormat),
				EncoderWireFormat:    encoderWireFormat(encoding.WireFormat),
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
		handleDefs := BuildHandleDefs(encodeFailure.HandleDefs)
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(encodeFailure.Value, decl)
		valueBuild := valueBuilder.String()
		errorCode := cppErrorCode(encodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(encodeFailure.HandleDefs) > 0
		for _, wireFormat := range supportedWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:              testCaseName(encodeFailure.Name, wireFormat),
				HandleDefs:        handleDefs,
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				ValueType:         declName(decl),
				ErrorCode:         errorCode,
				FuchsiaOnly:       fuchsiaOnly,
				EncoderWireFormat: encoderWireFormat(wireFormat),
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		decl, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		handleDefs := BuildHandleInfoDefs(decodeFailure.HandleDefs)
		valueType := cppConformanceType(decodeFailure.Type)
		errorCode := cppErrorCode(decodeFailure.Err)
		fuchsiaOnly := decl.IsResourceType() || len(decodeFailure.HandleDefs) > 0
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:             testCaseName(decodeFailure.Name, encoding.WireFormat),
				HandleDefs:       handleDefs,
				ValueType:        valueType,
				Bytes:            BuildBytes(encoding.Bytes),
				Handles:          BuildRawHandleInfos(encoding.Handles),
				ErrorCode:        errorCode,
				FuchsiaOnly:      fuchsiaOnly,
				WireFormatHeader: wireFormatHeader(encoding.WireFormat),
			})
		}
	}
	return decodeFailureCases, nil
}

func wireFormatHeader(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("fidl::test::util::k%sHeader", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func encoderWireFormat(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("fidl::internal::WireFormatVersion::k%s", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

var supportedWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
	gidlir.V2WireFormat,
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	for _, wf := range supportedWireFormats {
		if wireFormat == wf {
			return true
		}
	}
	return false
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName,
		fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func cppErrorCode(code gidlir.ErrorCode) string {
	// TODO(fxbug.dev/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}

func cppConformanceType(gidlTypeString string) string {
	return "conformance::" + gidlTypeString
}
