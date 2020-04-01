// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"io"
	"strconv"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
package fidl_test

import (
	"reflect"
	"testing"

	"fidl/conformance"

	"syscall/zx/fidl"
)

{{ if .EncodeSuccessCases }}
func TestAllEncodeSuccessCases(t *testing.T) {
{{ range .EncodeSuccessCases }}
	{
		{{ .ValueBuild }}
		encodeSuccessCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .ValueVar }},
			bytes: {{ .Bytes }},
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .DecodeSuccessCases }}
func TestAllDecodeSuccessCases(t *testing.T) {
{{ range .DecodeSuccessCases }}
	{
		{{ .ValueBuild }}
		decodeSuccessCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .ValueVar }},
			bytes: {{ .Bytes }},
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .EncodeFailureCases }}
func TestAllEncodeFailureCases(t *testing.T) {
{{ range .EncodeFailureCases }}
	{
		{{ .ValueBuild }}
		encodeFailureCase{
			name: {{ .Name }},
			context: {{ .Context }},
			input: &{{ .ValueVar }},
			code: {{ .ErrorCode }},
		}.check(t)
	}
{{ end }}
}
{{ end }}

{{ if .DecodeFailureCases }}
func TestAllDecodeFailureCases(t *testing.T) {
{{ range .DecodeFailureCases }}
	{
		decodeFailureCase{
			name: {{ .Name }},
			context: {{ .Context }},
			valTyp: reflect.TypeOf((*{{ .ValueType }})(nil)),
			bytes: {{ .Bytes }},
			code: {{ .ErrorCode }},
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
	Name, Context, ValueBuild, ValueVar, Bytes string
}

type decodeSuccessCase struct {
	Name, Context, ValueBuild, ValueVar, Bytes string
}

type encodeFailureCase struct {
	Name, Context, ValueBuild, ValueVar, ErrorCode string
}

type decodeFailureCase struct {
	Name, Context, ValueType, Bytes, ErrorCode string
}

// GenerateConformanceTests generates Go tests.
func GenerateConformanceTests(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, fidl)
	if err != nil {
		return err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, fidl)
	if err != nil {
		return err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, fidl)
	if err != nil {
		return err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, fidl)
	if err != nil {
		return err
	}
	input := conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	}
	return conformanceTmpl.Execute(wr, input)
}

func marshalerContext(wireFormat gidlir.WireFormat) string {
	switch wireFormat {
	case gidlir.OldWireFormat:
		return `fidl.MarshalerContext{
			DecodeUnionsFromXUnionBytes: false,
			EncodeUnionsAsXUnionBytes:   false,
		}`
	case gidlir.V1WireFormat:
		return `fidl.MarshalerContext{
			DecodeUnionsFromXUnionBytes: true,
			EncodeUnionsAsXUnionBytes:   true,
		}`
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, fidl fidlir.Root) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(encodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeSuccess %s: %s", encodeSuccess.Name, err)
		}

		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range encodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:    marshalerContext(encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				Bytes:      bytesBuilder(encoding.Bytes),
			})
		}
	}
	return encodeSuccessCases, nil
}

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, fidl fidlir.Root) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(decodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("decodeSuccess %s: %s", decodeSuccess.Name, err)
		}

		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, decodeSuccess.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, encoding := range decodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:    marshalerContext(encoding.WireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				Bytes:      bytesBuilder(encoding.Bytes),
			})
		}
	}
	return decodeSuccessCases, nil
}

func encodeFailureCases(gidlEncodeFailures []gidlir.EncodeFailure, fidl fidlir.Root) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailures {
		decl, err := gidlmixer.ExtractDeclarationUnsafe(encodeFailure.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeFailure %s: %s", encodeFailure.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeFailure.Value) {
			continue
		}
		code, err := goErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, err
		}

		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeFailure.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		for _, wireFormat := range encodeFailure.WireFormats {
			if wireFormat == gidlir.OldWireFormat {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:       testCaseName(encodeFailure.Name, wireFormat),
				Context:    marshalerContext(wireFormat),
				ValueBuild: valueBuild,
				ValueVar:   valueVar,
				ErrorCode:  code,
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, fidl fidlir.Root) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		code, err := goErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, err
		}

		valueType := "conformance." + decodeFailure.Type
		for _, encoding := range decodeFailure.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:      testCaseName(decodeFailure.Name, encoding.WireFormat),
				Context:   marshalerContext(encoding.WireFormat),
				ValueType: valueType,
				Bytes:     bytesBuilder(encoding.Bytes),
				ErrorCode: code,
			})
		}
	}
	return decodeFailureCases, nil
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return strconv.Quote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}
