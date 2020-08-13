// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dart

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlconfig "gidl/config"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
// Ignore unused imports so that GIDL tests can be commented out without error.
// ignore_for_file: unused_import

import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:fidl/fidl.dart' as fidl;
import 'package:topaz.lib.gidl/gidl.dart';

import 'package:fidl_conformance/fidl_async.dart';

void main() {
	group('conformance', () {
		group('encode success cases', () {
			{{ range .EncodeSuccessCases }}
			EncodeSuccessCase.run(
				{{ .EncoderName }},
				{{ .Name }},
				{{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }});
			{{ end }}
				});

		group('decode success cases', () {
			{{ range .DecodeSuccessCases }}
			DecodeSuccessCase.run(
				{{ .DecoderName }},
				{{ .Name }},
				{{ .Value }},
				{{ .ValueType }},
				{{ .Bytes }});
			{{ end }}
				});

		group('encode failure cases', () {
			{{ range .EncodeFailureCases }}
			EncodeFailureCase.run(
				{{ .EncoderName }},
				{{ .Name }},
				() => {{ .Value }},
				{{ .ValueType }},
				{{ .ErrorCode }});
			{{ end }}
				});

		group('decode failure cases', () {
			{{ range .DecodeFailureCases }}
			DecodeFailureCase.run(
				{{ .DecoderName }},
				{{ .Name }},
				{{ .ValueType }},
				{{ .Bytes }},
				{{ .ErrorCode }});
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
	EncoderName, Name, Value, ValueType, Bytes string
}

type decodeSuccessCase struct {
	DecoderName, Name, Value, ValueType, Bytes string
}

type encodeFailureCase struct {
	EncoderName, Name, Value, ValueType, ErrorCode string
}

type decodeFailureCase struct {
	DecoderName, Name, ValueType, Bytes, ErrorCode string
}

// Generate generates dart tests.
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
	var buf bytes.Buffer
	err = tmpl.Execute(&buf, tmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
	return buf.Bytes(), nil, err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		valueStr := visit(encodeSuccess.Value, decl)
		valueType := typeName(decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				EncoderName: encoderName(encoding.WireFormat),
				Name:        testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Value:       valueStr,
				ValueType:   valueType,
				Bytes:       bytesBuilder(encoding.Bytes),
			})
		}
	}
	return encodeSuccessCases, nil
}

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, schema gidlmixer.Schema) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := schema.ExtractDeclaration(decodeSuccess.Value)
		if err != nil {
			return nil, fmt.Errorf("decode success %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		valueStr := visit(decodeSuccess.Value, decl)
		valueType := typeName(decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				DecoderName: decoderName(encoding.WireFormat),
				Name:        testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Value:       valueStr,
				ValueType:   valueType,
				Bytes:       bytesBuilder(encoding.Bytes),
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
		errorCode, err := dartErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		valueStr := visit(encodeFailure.Value, decl)
		valueType := typeName(decl)
		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				EncoderName: encoderName(wireFormat),
				Name:        testCaseName(encodeFailure.Name, wireFormat),
				Value:       valueStr,
				ValueType:   valueType,
				ErrorCode:   errorCode,
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
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				DecoderName: decoderName(encoding.WireFormat),
				Name:        testCaseName(decodeFailure.Name, encoding.WireFormat),
				ValueType:   valueType,
				Bytes:       bytesBuilder(encoding.Bytes),
				ErrorCode:   errorCode,
			})
		}
	}
	return decodeFailureCases, nil
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fidlcommon.SingleQuote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}

func encoderName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("Encoders.%s", wireFormat)
}

func decoderName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("Decoders.%s", wireFormat)
}

func dartTypeName(inputType string) string {
	return fmt.Sprintf("k%s_Type", inputType)
}

func bytesBuilder(bytes []byte) string {
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

// Dart error codes defined in: topaz/public/dart/fidl/lib/src/error.dart.
var dartErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "fidlStringTooLong",
	gidlir.StringNotUtf8:              "unknown",
	gidlir.NonEmptyStringWithNullBody: "fidlNonNullableTypeWithNullValue",
	gidlir.StrictUnionFieldNotSet:     "fidlStrictXUnionFieldNotSet",
	gidlir.StrictUnionUnknownField:    "fidlStrictXUnionUnknownField",
	gidlir.StrictEnumUnknownValue:     "fidlInvalidEnumValue",
	gidlir.InvalidPaddingByte:         "unknown",
}

func dartErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := dartErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.FidlErrorCode.%s", str), nil
	}
	return "", fmt.Errorf("no dart error string defined for error code %s", code)
}
