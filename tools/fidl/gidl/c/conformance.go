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

#include <conformance/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/c/walker_tests/conformance_test_utils.h"

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#include "sdk/cts/tests/pkg/fidl/cpp/test/handle_util.h"
#endif

{{ range .DecodeSuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(C_Conformance, {{ .Name }}_Decode) {
	{{- if .HandleDefs }}
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	[[maybe_unused]] fidl::Arena<ZX_CHANNEL_MAX_MSG_BYTES> allocator;
	{{ .ValueBuild }}
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_t> handles = {{ .Handles }};
	auto obj = {{ .ValueVar }};
	EXPECT_TRUE(c_conformance_utils::DecodeSuccess({{ .WireFormatVersion }}, decltype(obj)::Type, std::move(bytes), std::move(handles)));
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
	const std::vector<zx_handle_t> handle_defs = {{ .HandleDefs }};
	{{- end }}
	std::vector<uint8_t> bytes = {{ .Bytes }};
	std::vector<zx_handle_t> handles = {{ .Handles }};
	EXPECT_TRUE(c_conformance_utils::DecodeFailure({{ .WireFormatVersion }}, {{ .ValueType }}::Type, std::move(bytes), std::move(handles), {{ .ErrorCode }}));
	{{- if .HandleDefs }}
	for (const zx_handle_t handle : handle_defs) {
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
	DecodeSuccessCases []decodeSuccessCase
	DecodeFailureCases []decodeFailureCase
}

type decodeSuccessCase struct {
	Name, HandleDefs, ValueBuild, ValueVar, Bytes, Handles, WireFormatVersion string
	FuchsiaOnly                                                               bool
}

type decodeFailureCase struct {
	Name, HandleDefs, ValueType, Bytes, Handles, ErrorCode, WireFormatVersion string
	FuchsiaOnly                                                               bool
}

// Generate generates C tests.
func GenerateConformanceTests(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
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
		DecodeSuccessCases: decodeSuccessCases,
		DecodeFailureCases: decodeFailureCases,
	})
	return buf.Bytes(), err
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
		valueBuild, valueVar := libllcpp.BuildValueAllocator("allocator", decodeSuccess.Value, decl, libllcpp.HandleReprRaw)
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:              testCaseName(decodeSuccess.Name, encoding.WireFormat),
				HandleDefs:        handleDefs,
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				Handles:           libhlcpp.BuildRawHandles(encoding.Handles),
				FuchsiaOnly:       fuchsiaOnly,
				WireFormatVersion: wireFormatName(encoding.WireFormat),
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
		handleDefs := libhlcpp.BuildHandleDefs(decodeFailure.HandleDefs)
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
				Handles:           libhlcpp.BuildRawHandles(encoding.Handles),
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
