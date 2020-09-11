// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"bytes"
	"fmt"
	"strconv"
	"text/template"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
package fidl_test

import (
	"reflect"
	"testing"

	"fidl/conformance"

	"syscall/zx"
	"syscall/zx/fidl"
)

// Introduce a dependency on reflect to avoid unused import errors if certain
// GIDL tests are disabled.
var _ = reflect.Copy

{{ if .EncodeSuccessCases }}
func TestAllEncodeSuccessCases(t *testing.T) {
{{ range .EncodeSuccessCases }}
	{
	{{- if .HandleDefs }}
		handleTypes := {{ .HandleDefs }}
		handles := createHandles(handleTypes)
	{{- end }}
		encodeSuccessCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .Value }},
			bytes: {{ .Bytes }},
	{{- if .HandleDefs }}
			handleInfos: []zx.HandleInfo{
		{{- range .Handles }}
				{
					Handle: handles[{{ . }}],
					Type: handleTypes[{{ . }}],
				},
		{{- end }}
			},
	{{- end }}
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .DecodeSuccessCases }}
func TestAllDecodeSuccessCases(t *testing.T) {
{{ range .DecodeSuccessCases }}
	{
	{{- if .HandleDefs }}
		handleTypes := {{ .HandleDefs }}
		handles := createHandles(handleTypes)
	{{- end }}
		decodeSuccessCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .Value }},
			bytes: {{ .Bytes }},
	{{- if .HandleDefs }}
			handleInfos: []zx.HandleInfo{
		{{- range .Handles }}
				{
					Handle: handles[{{ . }}],
					Type: handleTypes[{{ . }}],
				},
		{{- end }}
			},
	{{- end }}
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .EncodeFailureCases }}
func TestAllEncodeFailureCases(t *testing.T) {
{{ range .EncodeFailureCases }}
	{
	{{- if .HandleDefs }}
		handles := createHandles({{ .HandleDefs }})
	{{- end }}
		encodeFailureCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .Value }},
			code: {{ .ErrorCode }},
	{{- if .HandleDefs }}
			handles: handles,
	{{- end }}
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .DecodeFailureCases }}
func TestAllDecodeFailureCases(t *testing.T) {
{{ range .DecodeFailureCases }}
	{
	{{- if .HandleDefs }}
		handleTypes := {{ .HandleDefs }}
		handles := createHandles(handleTypes)
	{{- end }}
		decodeFailureCase{
			name: {{ .Name }},
			context: {{ .Context }},
			valTyp: reflect.TypeOf((*{{ .ValueType }})(nil)),
			bytes: {{ .Bytes }},
			code: {{ .ErrorCode }},
	{{- if .HandleDefs }}
			handleInfos: []zx.HandleInfo{
		{{- range .Handles }}
				{
					Handle: handles[{{ . }}],
					Type: handleTypes[{{ . }}],
				},
		{{- end }}
			},
	{{- end }}
		}.check(t)
	}
{{ end }}
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
	Name, Context, Value, Bytes, HandleDefs string
	Handles                                 []gidlir.Handle
}

type decodeSuccessCase struct {
	Name, Context, Value, Bytes, HandleDefs string
	Handles                                 []gidlir.Handle
}

type encodeFailureCase struct {
	Name, Context, Value, ErrorCode, HandleDefs string
}

type decodeFailureCase struct {
	Name, Context, ValueType, Bytes, ErrorCode, HandleDefs string
	Handles                                                []gidlir.Handle
}

// GenerateConformanceTests generates Go tests.
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
	err = withGoFmt{conformanceTmpl}.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func marshalerContext(wireFormat gidlir.WireFormat) string {
	switch wireFormat {
	case gidlir.V1WireFormat:
		return `fidl.MarshalerContext{}`
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		value := visit(encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:    marshalerContext(encoding.WireFormat),
				Value:      value,
				Bytes:      buildBytes(encoding.Bytes),
				HandleDefs: buildHandleDefs(encodeSuccess.HandleDefs),
				Handles:    encoding.Handles,
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
		value := visit(decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:    marshalerContext(encoding.WireFormat),
				Value:      value,
				Bytes:      buildBytes(encoding.Bytes),
				HandleDefs: buildHandleDefs(decodeSuccess.HandleDefs),
				Handles:    encoding.Handles,
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
		code, err := goErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		value := visit(encodeFailure.Value, decl)
		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:       testCaseName(encodeFailure.Name, wireFormat),
				Context:    marshalerContext(wireFormat),
				Value:      value,
				ErrorCode:  code,
				HandleDefs: buildHandleDefs(encodeFailure.HandleDefs),
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
		code, err := goErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		valueType := declName(decl)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:       testCaseName(decodeFailure.Name, encoding.WireFormat),
				Context:    marshalerContext(encoding.WireFormat),
				ValueType:  valueType,
				Bytes:      buildBytes(encoding.Bytes),
				ErrorCode:  code,
				HandleDefs: buildHandleDefs(decodeFailure.HandleDefs),
				Handles:    encoding.Handles,
			})
		}
	}
	return decodeFailureCases, nil
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return strconv.Quote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}
