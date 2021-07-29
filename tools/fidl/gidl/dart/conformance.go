// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dart

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
// @dart = 2.8

// Ignore unused imports so that GIDL tests can be commented out without error.
// ignore_for_file: unused_import

import 'dart:typed_data';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_conformance/fidl_async.dart';
import 'package:test/test.dart';
import 'package:topaz.lib.gidl/gidl.dart';
import 'package:topaz.lib.gidl/handles.dart';
import 'package:zircon/zircon.dart';

void main() {
	group('conformance', () {
		group('encode success cases', () {
			{{ range .EncodeSuccessCases }}
			{{- if .HandleDefs }}
			EncodeSuccessCase.runWithHandles(
				{{ .EncoderName }},
				{{ .Name }},
				(List<Handle> handles) => {{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }},
				{{ .HandleDefs }},
				{{ .Handles }});
			{{- else }}
			EncodeSuccessCase.run(
				{{ .EncoderName }},
				{{ .Name }},
				{{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }});
            {{- end }}
			{{ end }}
				});

		group('decode success cases', () {
			{{ range .DecodeSuccessCases }}
			{{- if .HandleDefs }}
			DecodeSuccessCase.runWithHandles(
				{{ .Name }},
				{{ .WireFormat }},
				(List<Handle> handles) => {{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }},
				{{ .HandleDefs }},
				{{ .Handles }},
				{{ .UnusedHandles }});
			{{- else }}
			DecodeSuccessCase.run(
				{{ .Name }},
				{{ .WireFormat }},
				{{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }});
            {{- end }}
			{{ end }}
				});

		group('encode failure cases', () {
			{{ range .EncodeFailureCases }}
			{{- if .HandleDefs }}
			EncodeFailureCase.runWithHandles(
				{{ .EncoderName }},
				{{ .Name }},
				(List<Handle> handles) => {{ .Value }},
				{{ .ValueType }},
				{{ .ErrorCode }},
				{{ .HandleDefs }});
			{{- else }}
			EncodeFailureCase.run(
				{{ .EncoderName }},
				{{ .Name }},
				() => {{ .Value }},
				{{ .ValueType }},
				{{ .ErrorCode }});
			{{- end }}
			{{ end }}
				});

		group('decode failure cases', () {
			{{ range .DecodeFailureCases }}
			DecodeFailureCase.run(
				{{ .Name }},
				{{ .WireFormat }},
				{{ .ValueType }},
				{{ .Bytes }},
			{{- if .HandleDefs }}
				{{ .ErrorCode }},
				{{ .HandleDefs }},
				{{ .Handles }});
			{{- else }}
				{{ .ErrorCode }});
			{{- end }}
			{{ end }}
				});
	});
}
`))

type tmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
	DecodeSuccessCases []decodeSuccessCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	EncoderName, Name, Value, ValueType, Bytes, HandleDefs, Handles string
}

type decodeSuccessCase struct {
	Name, WireFormat, Value, ValueType, Bytes, HandleDefs, Handles, UnusedHandles string
}

type encodeFailureCase struct {
	EncoderName, Name, Value, ValueType, ErrorCode, HandleDefs string
}

type decodeFailureCase struct {
	Name, WireFormat, ValueType, Bytes, ErrorCode, HandleDefs, Handles string
}

// Generate generates dart tests.
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
	err = tmpl.Execute(&buf, tmplInput{
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
		valueStr := visit(encodeSuccess.Value, decl)
		valueType := typeName(decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupportedForEncode(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				EncoderName: encoderName(encoding.WireFormat),
				Name:        testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Value:       valueStr,
				ValueType:   valueType,
				Bytes:       buildBytes(encoding.Bytes),
				HandleDefs:  buildHandleDefs(encodeSuccess.HandleDefs),
				Handles:     toDartIntList(gidlir.GetHandlesFromHandleDispositions(encoding.HandleDispositions)),
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
		valueStr := visit(decodeSuccess.Value, decl)
		valueType := typeName(decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupportedForDecode(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				WireFormat: wireFormatName(encoding.WireFormat),
				Value:      valueStr,
				ValueType:  valueType,
				Bytes:      buildBytes(encoding.Bytes),
				HandleDefs: buildHandleDefs(decodeSuccess.HandleDefs),
				Handles:    toDartIntList(encoding.Handles),
				UnusedHandles: toDartIntList(gidlir.GetUnusedHandles(decodeSuccess.Value,
					encoding.Handles)),
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
		errorCode, err := dartErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		valueStr := visit(encodeFailure.Value, decl)
		valueType := typeName(decl)
		for _, wireFormat := range supportedEncodeWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				EncoderName: encoderName(wireFormat),
				Name:        testCaseName(encodeFailure.Name, wireFormat),
				Value:       valueStr,
				ValueType:   valueType,
				ErrorCode:   errorCode,
				HandleDefs:  buildHandleDefs(encodeFailure.HandleDefs),
			})
		}
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure, schema gidlmixer.Schema) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		_, err := schema.ExtractDeclarationByName(decodeFailure.Type)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		errorCode, err := dartErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		valueType := dartTypeName(decodeFailure.Type)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupportedForDecode(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:       testCaseName(decodeFailure.Name, encoding.WireFormat),
				WireFormat: wireFormatName(encoding.WireFormat),
				ValueType:  valueType,
				Bytes:      buildBytes(encoding.Bytes),
				ErrorCode:  errorCode,
				HandleDefs: buildHandleDefs(decodeFailure.HandleDefs),
				Handles:    toDartIntList(encoding.Handles),
			})
		}
	}
	return decodeFailureCases, nil
}

var supportedEncodeWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
}
var supportedDecodeWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
	gidlir.V2WireFormat,
}

func wireFormatSupportedForEncode(wireFormat gidlir.WireFormat) bool {
	for _, wf := range supportedEncodeWireFormats {
		if wireFormat == wf {
			return true
		}
	}
	return false
}
func wireFormatSupportedForDecode(wireFormat gidlir.WireFormat) bool {
	for _, wf := range supportedDecodeWireFormats {
		if wireFormat == wf {
			return true
		}
	}
	return false
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fidlgen.SingleQuote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}

func encoderName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("Encoders.%s", wireFormat)
}

func wireFormatName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("fidl.WireFormat.%s", wireFormat)
}

func dartTypeName(inputType string) string {
	return fmt.Sprintf("k%s_Type", inputType)
}

func buildBytes(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("Uint8List.fromList([\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x,", b))
		if i%8 == 7 {
			// Note: empty comments are written to preserve formatting. See:
			// https://github.com/dart-lang/dart_style/wiki/FAQ#why-does-the-formatter-mess-up-my-collection-literals
			builder.WriteString(" //\n")
		}
	}
	builder.WriteString("])")
	return builder.String()
}

func toDartStr(value string) string {
	var buf bytes.Buffer
	buf.WriteRune('\'')
	for _, r := range value {
		if 0x20 <= r && r <= 0x7e { // printable ASCII rune
			buf.WriteRune(r)
		} else {
			buf.WriteString(fmt.Sprintf(`\u{%x}`, r))
		}
	}
	buf.WriteRune('\'')
	return buf.String()
}

func toDartIntList(handles []gidlir.Handle) string {
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, handle := range handles {
		builder.WriteString(fmt.Sprintf("%d,", handle))
		if i%8 == 7 {
			// Note: empty comments are written to preserve formatting. See:
			// https://github.com/dart-lang/dart_style/wiki/FAQ#why-does-the-formatter-mess-up-my-collection-literals
			builder.WriteString(" //\n")
		}
	}
	builder.WriteString("]")
	return builder.String()
}

// Dart error codes are defined in sdk/dart/fidl/lib/src/error.dart.
var dartErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.ExceededMaxOutOfLineDepth:    "fidlExceededMaxOutOfLineDepth",
	gidlir.ExtraHandles:                 "fidlTooManyHandles",
	gidlir.IncorrectHandleType:          "fidlIncorrectHandleType",
	gidlir.InvalidNumBytesInEnvelope:    "fidlInvalidNumBytesInEnvelope",
	gidlir.InvalidNumHandlesInEnvelope:  "fidlInvalidNumHandlesInEnvelope",
	gidlir.InvalidPaddingByte:           "unknown",
	gidlir.InvalidPresenceIndicator:     "fidlInvalidPresenceIndicator",
	gidlir.MissingRequiredHandleRights:  "fidlMissingRequiredHandleRights",
	gidlir.NonEmptyStringWithNullBody:   "fidlNonEmptyStringWithNullBody",
	gidlir.NonNullableTypeWithNullValue: "fidlNonNullableTypeWithNullValue",
	gidlir.NonResourceUnknownHandles:    "fidlNonResourceHandle",
	gidlir.StrictBitsUnknownBit:         "fidlInvalidBit",
	gidlir.StrictEnumUnknownValue:       "fidlInvalidEnumValue",
	gidlir.StrictUnionUnknownField:      "fidlStrictXUnionUnknownField",
	gidlir.StringNotUtf8:                "unknown",
	gidlir.StringTooLong:                "fidlStringTooLong",
	gidlir.TooFewHandles:                "fidlTooFewHandles",
	gidlir.UnionFieldNotSet:             "unknown",
	gidlir.InvalidInlineBitInEnvelope:   "fidlInvalidInlineBitInEnvelope",
}

func dartErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := dartErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.FidlErrorCode.%s", str), nil
	}
	return "", fmt.Errorf("no dart error string defined for error code %s", code)
}
