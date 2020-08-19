// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
#![cfg(test)]
#![allow(unused_imports)]

use {
	fidl::{Error, encoding::{Context, Decodable, Decoder, Encoder}},
	fidl_conformance as conformance,
	matches::assert_matches,
};

const V1_CONTEXT: &Context = &Context {};

{{ range .EncodeSuccessCases }}
#[test]
fn test_{{ .Name }}_encode() {
	let value = &mut {{ .Value }};
	let bytes = &mut Vec::new();
	bytes.resize(65536, 0xcd); // fill with junk data
	Encoder::encode_with_context({{ .Context }}, bytes, &mut Vec::new(), value).unwrap();
	assert_eq!(*bytes, &{{ .Bytes }}[..]);
}
{{ end }}

{{ range .DecodeSuccessCases }}
#[test]
fn test_{{ .Name }}_decode() {
	let value = &mut {{ .ValueType }}::new_empty();
	let bytes = &mut {{ .Bytes }};
	Decoder::decode_with_context({{ .Context }}, bytes, &mut [], value).unwrap();
	assert_eq!(*value, {{ .Value }});
}
{{ end }}

{{ range .EncodeFailureCases }}
#[test]
fn test_{{ .Name }}_encode_failure() {
	let value = &mut {{ .Value }};
	let bytes = &mut Vec::new();
	bytes.resize(65536, 0xcd); // fill with junk data
	match Encoder::encode_with_context({{ .Context }}, bytes, &mut Vec::new(), value) {
		Err(err) => assert_matches!(err, {{ .ErrorCode }} { .. }),
		Ok(_) => panic!("unexpected successful encoding"),
	}
}
{{ end }}

{{ range .DecodeFailureCases }}
#[test]
fn test_{{ .Name }}_decode_failure() {
	let value = &mut {{ .ValueType }}::new_empty();
	let bytes = &mut {{ .Bytes }};
	match Decoder::decode_with_context({{ .Context }}, bytes, &mut [], value) {
		Err(err) => assert_matches!(err, {{ .ErrorCode }} { .. }),
		Ok(_) => panic!("unexpected successful decoding"),
	}
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
	Name, Context, Value, Bytes string
}

type decodeSuccessCase struct {
	Name, Context, ValueType, Value, Bytes string
}

type encodeFailureCase struct {
	Name, Context, Value, ErrorCode string
}

type decodeFailureCase struct {
	Name, Context, ValueType, Bytes, ErrorCode string
}

// GenerateConformanceTests generates Rust tests.
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
	err = conformanceTmpl.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclaration(encodeSuccess.Value)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		value := visit(encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:    testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context: encodingContext(encoding.WireFormat),
				Value:   value,
				Bytes:   bytesBuilder(encoding.Bytes),
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
		valueType := declName(decl)
		value := visit(decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:      testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:   encodingContext(encoding.WireFormat),
				ValueType: valueType,
				Value:     value,
				Bytes:     bytesBuilder(encoding.Bytes),
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
		errorCode, err := rustErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		value := visit(encodeFailure.Value, decl)

		for _, wireFormat := range encodeFailure.WireFormats {
			if !wireFormatSupported(wireFormat) {
				continue
			}
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:      testCaseName(encodeFailure.Name, wireFormat),
				Context:   encodingContext(wireFormat),
				Value:     value,
				ErrorCode: errorCode,
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
		errorCode, err := rustErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("decode failure %s: %s", decodeFailure.Name, err)
		}
		valueType := declName(decl)
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:      testCaseName(decodeFailure.Name, encoding.WireFormat),
				Context:   encodingContext(encoding.WireFormat),
				ValueType: valueType,
				Bytes:     bytesBuilder(encoding.Bytes),
				ErrorCode: errorCode,
			})
		}
	}
	return decodeFailureCases, nil
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fidlcommon.ToSnakeCase(fmt.Sprintf("%s_%s", baseName, wireFormat))
}

func wireFormatSupported(wireFormat gidlir.WireFormat) bool {
	return wireFormat == gidlir.V1WireFormat
}

func encodingContext(wireFormat gidlir.WireFormat) string {
	switch wireFormat {
	case gidlir.V1WireFormat:
		return "V1_CONTEXT"
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x,", b))
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("]")
	return builder.String()
}

// Rust errors are defined in src/lib/fidl/rust/fidl/src/error.rs
var rustErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "OutOfRange",
	gidlir.StringNotUtf8:              "Utf8Error",
	gidlir.NonEmptyStringWithNullBody: "UnexpectedNullRef",
	gidlir.StrictUnionFieldNotSet:     "UnknownUnionTag",
	gidlir.StrictUnionUnknownField:    "UnknownUnionTag",
	gidlir.StrictBitsUnknownBit:       "Invalid",
	gidlir.StrictEnumUnknownValue:     "Invalid",
	gidlir.ExceededMaxOutOfLineDepth:  "MaxRecursionDepth",
	gidlir.InvalidPaddingByte:         "NonZeroPadding",
}

func rustErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := rustErrorCodeNames[code]; ok {
		return fmt.Sprintf("Error::%s", str), nil
	}
	return "", fmt.Errorf("no rust error string defined for error code %s", code)
}
