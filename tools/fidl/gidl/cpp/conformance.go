// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("tmpl").Parse(`
#include <zxtest/zxtest.h>

#include <fidl/test.conformance/cpp/natural_types.h>

#include "src/lib/fidl/cpp/tests/conformance/conformance_utils.h"

{{ range .EncodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Encode) {
	{{- if .HandleDefs }}
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	const auto expected_bytes = {{ .Bytes }};
	const auto expected_handles = {{ .Handles }};
	conformance_utils::EncodeSuccess(
		{{ .WireFormatVersion }}, obj, expected_bytes, expected_handles, {{ .CheckHandleRights }});
}
{{ end }}

{{ range .DecodeSuccessCases }}
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
	auto handles = {{ .Handles }};
	conformance_utils::DecodeSuccess<{{ .Type }}>(
		{{ .WireFormatVersion }}, bytes, handles, [&]({{ .Type }}& value) {
		{{ .EqualityCheck }}
	});
}
{{ end }}

{{ range .EncodeFailureCases }}
TEST(Conformance, {{ .Name }}_EncodeFailure) {
	{{- if .HandleDefs }}
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	const std::vector<zx_handle_t> handle_defs;
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	conformance_utils::EncodeFailure(
	{{ .WireFormatVersion }}, obj);
	{{- if .HandleDefs }}
	for (const auto handle_def : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(
			handle_def, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
	}
	{{- end }}
}
{{ end }}

{{ range .DecodeFailureCases }}
TEST(Conformance, {{ .Name }}_DecodeFailure) {
	{{- if .HandleDefs }}
	const auto handle_defs = {{ .HandleDefs }};
	{{- end }}
	auto bytes = {{ .Bytes }};
	auto handles = {{ .Handles }};
	conformance_utils::DecodeFailure<{{ .Type }}>(
		{{ .WireFormatVersion }}, bytes, handles);
	{{- if .HandleDefs }}
	for (const auto handle_def : handle_defs) {
		EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_get_info(
			handle_def.handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
	}
	{{- end }}
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
	WireFormatVersion, Name, ValueBuild, ValueVar, Bytes, HandleDefs, Handles string
	CheckHandleRights                                                         bool
}

type decodeSuccessCase struct {
	WireFormatVersion, Name, Type, Bytes, HandleDefs, Handles, EqualityCheck, HandleKoidVectorName string
}

type encodeFailureCase struct {
	WireFormatVersion, Name, ValueBuild, ValueVar, HandleDefs string
}

type decodeFailureCase struct {
	WireFormatVersion, Name, Type, Bytes, HandleDefs, Handles string
}

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
		valueBuild, valueVar := buildValue(encodeSuccess.Value, decl, handleReprRaw)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:              testCaseName(encodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:        buildHandleDefs(encodeSuccess.HandleDefs),
				Handles:           libhlcpp.BuildRawHandleDispositions(encoding.HandleDispositions),
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
			return nil, fmt.Errorf("encode success %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		actualValueVar := "value"
		handleKoidVectorName := "handle_koids"
		equalityCheck := buildEqualityCheck(actualValueVar, decodeSuccess.Value, decl, handleKoidVectorName)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                 testCaseName(decodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion:    wireFormatVersionName(encoding.WireFormat),
				Type:                 conformanceType(gidlir.TypeFromValue(decodeSuccess.Value)),
				Bytes:                libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:           buildHandleInfoDefs(decodeSuccess.HandleDefs),
				Handles:              libhlcpp.BuildRawHandleInfos(encoding.Handles),
				EqualityCheck:        equalityCheck,
				HandleKoidVectorName: handleKoidVectorName,
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
		valueBuild, valueVar := buildValue(encodeFailure.Value, decl, handleReprDisposition)
		for _, wireFormat := range supportedWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:              testCaseName(encodeFailure.Name, wireFormat),
				WireFormatVersion: wireFormatVersionName(wireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				HandleDefs:        buildHandleDefs(encodeFailure.HandleDefs),
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:              testCaseName(decodeFailure.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				Type:              conformanceType(decodeFailure.Type),
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:        buildHandleInfoDefs(decodeFailure.HandleDefs),
				Handles:           libhlcpp.BuildRawHandleInfos(encoding.Handles),
			})
		}
	}
	return decodeFailureCases, nil
}

var supportedWireFormats = []gidlir.WireFormat{
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

func wireFormatVersionName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("::fidl::internal::WireFormatVersion::k%s", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func conformanceType(gidlTypeString string) string {
	// Note: only works for domain objects (not protocols & services)
	return "test_conformance::" + fidlgen.ToUpperCamelCase(gidlTypeString)
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName,
		fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func buildHandleDef(def gidlir.HandleDef) string {
	switch def.Subtype {
	case fidlgen.Channel:
		return fmt.Sprintf("conformance_utils::CreateChannel(%d)", def.Rights)
	case fidlgen.Event:
		return fmt.Sprintf("conformance_utils::CreateEvent(%d)", def.Rights)
	default:
		panic(fmt.Sprintf("unsupported handle subtype: %s", def.Subtype))
	}
}

func handleType(subtype fidlgen.HandleSubtype) string {
	switch subtype {
	case fidlgen.Channel:
		return "ZX_OBJ_TYPE_CHANNEL"
	case fidlgen.Event:
		return "ZX_OBJ_TYPE_EVENT"
	default:
		panic(fmt.Sprintf("unsupported handle subtype: %s", subtype))
	}
}

func buildHandleDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("std::vector<zx_handle_t>{\n")
	for i, d := range defs {
		builder.WriteString(buildHandleDef(d))
		// Write indices corresponding to the .gidl file handle_defs block.
		builder.WriteString(fmt.Sprintf(", // #%d\n", i))
	}
	builder.WriteString("}")
	return builder.String()
}

func buildHandleInfoDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("std::vector<zx_handle_info_t>{\n")
	for i, d := range defs {
		builder.WriteString(fmt.Sprintf(`
// #%d
zx_handle_info_t{
	.handle = %s,
	.type = %s,
	.rights = %d,
	.unused = 0u,
},
`, i, buildHandleDef(d), handleType(d.Subtype), d.Rights))
	}
	builder.WriteString("}")
	return builder.String()
}
