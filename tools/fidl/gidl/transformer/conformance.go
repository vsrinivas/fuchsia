// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package transformer

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
#include <fidl/conformance/cpp/wire.h>

#include <vector>

#include "src/lib/fidl/transformer/tests/conformance_util.h"

{{ range .SuccessCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(V1_TO_V2, {{ .Name }}) {
	const auto input_bytes = {{ .V1Bytes }};
	const auto expected_bytes = {{ .V2Bytes }};
	transformer_conformance_utils::FidlTransformSuccessCase<{{ .Type }}>(
		FIDL_TRANSFORMATION_V1_TO_V2, input_bytes, expected_bytes);
};

TEST(V2_TO_V1, {{ .Name }}) {
	const auto input_bytes = {{ .V2Bytes }};
	const auto expected_bytes = {{ .V1Bytes }};
	transformer_conformance_utils::FidlTransformSuccessCase<{{ .Type }}>(
		FIDL_TRANSFORMATION_V2_TO_V1, input_bytes, expected_bytes);
};
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .V1FailureCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(V1_TO_V2_FAILURE, {{ .Name }}) {
	const auto input_bytes = {{ .Bytes }};
	transformer_conformance_utils::FidlTransformFailureCase<{{ .Type }}>(
		FIDL_TRANSFORMATION_V1_TO_V2, input_bytes);
};
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}

{{ range .V2FailureCases }}
{{- if .FuchsiaOnly }}
#ifdef __Fuchsia__
{{- end }}
TEST(V2_TO_V1_FAILURE, {{ .Name }}) {
	const auto input_bytes = {{ .Bytes }};
	transformer_conformance_utils::FidlTransformFailureCase<{{ .Type }}>(
		FIDL_TRANSFORMATION_V2_TO_V1, input_bytes);
};
{{- if .FuchsiaOnly }}
#endif  // __Fuchsia__
{{- end }}
{{ end }}
`))

type conformanceTmplInput struct {
	SuccessCases   []successCase
	V1FailureCases []failureCase
	V2FailureCases []failureCase
}

type successCase struct {
	Name        string
	Type        string
	V1Bytes     string
	V2Bytes     string
	FuchsiaOnly bool
}

type failureCase struct {
	Name        string
	Type        string
	Bytes       string
	FuchsiaOnly bool
}

func GenerateConformanceTests(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	var input conformanceTmplInput

	for _, decodeSuccess := range gidl.DecodeSuccess {
		var v1Bytes []byte
		var v2Bytes []byte
		for _, encoding := range decodeSuccess.Encodings {
			switch encoding.WireFormat {
			case gidlir.V1WireFormat:
				v1Bytes = encoding.Bytes
			case gidlir.V2WireFormat:
				v2Bytes = encoding.Bytes
			}
		}
		if v1Bytes == nil || v2Bytes == nil {
			continue
		}
		schema := gidlmixer.BuildSchema(fidl)
		decl, err := schema.ExtractDeclaration(decodeSuccess.Value, decodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("failed to extract declaration %s: %w", decodeSuccess.Name, err)
		}
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		input.SuccessCases = append(input.SuccessCases, successCase{
			Name:        decodeSuccess.Name,
			Type:        libllcpp.ConformanceType(gidlir.TypeFromValue(decodeSuccess.Value)),
			V1Bytes:     libhlcpp.BuildBytes(v1Bytes),
			V2Bytes:     libhlcpp.BuildBytes(v2Bytes),
			FuchsiaOnly: fuchsiaOnly,
		})
	}

	for _, decodeFailure := range gidl.DecodeFailure {
		schema := gidlmixer.BuildSchema(fidl)
		decl, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("failed to extract declaration %s: %w", decodeFailure.Name, err)
		}
		fuchsiaOnly := decl.IsResourceType() || len(decodeFailure.HandleDefs) > 0
		for _, encoding := range decodeFailure.Encodings {
			failureCase := failureCase{
				Name:        decodeFailure.Name,
				Type:        libllcpp.ConformanceType(decodeFailure.Type),
				Bytes:       libhlcpp.BuildBytes(encoding.Bytes),
				FuchsiaOnly: fuchsiaOnly,
			}
			switch encoding.WireFormat {
			case gidlir.V1WireFormat:
				input.V1FailureCases = append(input.V1FailureCases, failureCase)
			case gidlir.V2WireFormat:
				input.V2FailureCases = append(input.V2FailureCases, failureCase)
			}
		}
	}

	var buf bytes.Buffer
	err := conformanceTmpl.Execute(&buf, input)
	return buf.Bytes(), err
}
