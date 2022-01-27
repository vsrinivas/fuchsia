// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	"fmt"
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
	const std::vector<zx_handle_t> handle_defs;
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	const auto expected_bytes = {{ .Bytes }};
	conformance_utils::EncodeSuccess(
		{{ .WireFormatVersion }}, obj, expected_bytes);
}
{{ end }}

{{ range .DecodeSuccessCases }}
TEST(Conformance, {{ .Name }}_Decode) {
	const std::vector<zx_handle_t> handle_defs;
	{{ .ValueBuild }}
	auto expected_obj = {{ .ValueVar }};
	auto bytes = {{ .Bytes }};
	conformance_utils::DecodeSuccess(
		{{ .WireFormatVersion }}, bytes, expected_obj);
}
{{ end }}

{{ range .EncodeFailureCases }}
TEST(Conformance, {{ .Name }}_EncodeFailure) {
	const std::vector<zx_handle_t> handle_defs;
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	conformance_utils::EncodeFailure(
		{{ .WireFormatVersion }}, obj);
}
{{ end }}

{{ range .DecodeFailureCases }}
TEST(Conformance, {{ .Name }}_DecodeFailure) {
	auto bytes = {{ .Bytes }};
	conformance_utils::DecodeFailure<{{ .Type }}>(
		{{ .WireFormatVersion }}, bytes);
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
	WireFormatVersion, Name, ValueBuild, ValueVar, Bytes string
}

type decodeSuccessCase struct {
	WireFormatVersion, Name, ValueBuild, ValueVar, Bytes string
}

type encodeFailureCase struct {
	WireFormatVersion, Name, ValueBuild, ValueVar string
}

type decodeFailureCase struct {
	WireFormatVersion, Name, Type, Bytes string
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
		if decl.IsResourceType() {
			// Handles not yet supported.
			continue
		}
		valueBuild, valueVar := buildValue(encodeSuccess.Value, decl)
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
		if decl.IsResourceType() {
			// Handles not yet supported.
			continue
		}
		valueBuild, valueVar := buildValue(decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:              testCaseName(decodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
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
		if decl.IsResourceType() {
			// Handles not yet supported.
			continue
		}
		valueBuild, valueVar := buildValue(encodeFailure.Value, decl)
		for _, wireFormat := range supportedWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:              testCaseName(encodeFailure.Name, wireFormat),
				WireFormatVersion: wireFormatVersionName(wireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
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
			return nil, fmt.Errorf("encode success %s: %s", decodeFailure.Name, err)
		}
		if decl.IsResourceType() {
			// Handles not yet supported.
			continue
		}
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:              testCaseName(decodeFailure.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				Type:              conformanceType(decodeFailure.Type),
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
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
