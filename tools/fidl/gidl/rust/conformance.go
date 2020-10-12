// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"text/template"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
#![cfg(test)]
#![allow(unused_imports)]

use {
	fidl::{AsHandleRef, Error, Handle, encoding::{Context, Decodable, Decoder, Encoder}},
	fidl_conformance as conformance,
	fuchsia_zircon_status::Status,
	gidl_util::{HandleSubtype, create_handles, copy_handle, copy_handles_at, disown_handles, get_info_handle_valid},
	matches::assert_matches,
};

const V1_CONTEXT: &Context = &Context {};

{{ range .EncodeSuccessCases }}
#[test]
fn test_{{ .Name }}_encode() {
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }}).unwrap();
	let handle_defs = unsafe { disown_handles(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let expected_handles = unsafe { disown_handles(copy_handles_at(handle_defs, &{{ .Handles }})) };
	let expected_handles = expected_handles.as_ref();
	{{- end }}
	let value = &mut {{ .Value }};
	let bytes = &mut Vec::new();
	let handles = &mut Vec::new();
	bytes.resize(65536, 0xcd); // fill with junk data
	Encoder::encode_with_context({{ .Context }}, bytes, handles, value).unwrap();
	assert_eq!(bytes, &{{ .Bytes }});
	{{- if .HandleDefs }}
	assert_eq!(handles, expected_handles);
	{{- else }}
	assert_eq!(handles, &[]);
	{{- end }}
}
{{ end }}

{{ range .DecodeSuccessCases }}
#[test]
fn test_{{ .Name }}_decode() {
	let bytes = &{{ .Bytes }};
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }}).unwrap();
	let handle_defs = unsafe { disown_handles(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let handles = &mut unsafe { copy_handles_at(handle_defs, &{{ .Handles }}) };
	{{- else }}
	let handles = &mut [];
	{{- end }}
	let value = &mut {{ .ValueType }}::new_empty();
	Decoder::decode_with_context({{ .Context }}, bytes, handles, value).unwrap();
	assert_eq!(value, &{{ .Value }});
	{{- if .HandleDefs }}
	// Re-encode purely for the side effect of linearizing the handles.
	let mut linear_handles = unsafe { disown_handles(Vec::new()) };
	let linear_handles = linear_handles.as_mut();
	Encoder::encode_with_context({{ .Context }}, &mut Vec::new(), linear_handles, value)
		.expect("Failed to re-encode the successfully decoded value");
	{{- end }}
}
{{ end }}

{{ range .EncodeFailureCases }}
#[test]
fn test_{{ .Name }}_encode_failure() {
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }}).unwrap();
	let handle_defs = unsafe { disown_handles(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	{{- end }}
	let value = &mut {{ .Value }};
	let bytes = &mut Vec::new();
	let handles = &mut Vec::new();
	bytes.resize(65536, 0xcd); // fill with junk data
	match Encoder::encode_with_context({{ .Context }}, bytes, handles, value) {
		Err(err) => assert_matches!(err, {{ .ErrorCode }} { .. }),
		Ok(_) => panic!("unexpected successful encoding"),
	}
	{{- if .HandleDefs }}
	assert_eq!(
		handle_defs.iter().map(get_info_handle_valid).collect::<Vec<_>>(),
		std::iter::repeat(Err(Status::BAD_HANDLE)).take(handle_defs.len()).collect::<Vec<_>>(),
	);
	{{- end }}
}
{{ end }}

{{ range .DecodeFailureCases }}
#[test]
fn test_{{ .Name }}_decode_failure() {
	let bytes = &{{ .Bytes }};
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }}).unwrap();
	let handle_defs = unsafe { disown_handles(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let handles = &mut unsafe { copy_handles_at(handle_defs, &{{ .Handles }}) };
	{{- else }}
	let handles = &mut [];
	{{- end }}
	let value = &mut {{ .ValueType }}::new_empty();
	match Decoder::decode_with_context({{ .Context }}, bytes, handles, value) {
		Err(err) => assert_matches!(err, {{ .ErrorCode }} { .. }),
		Ok(_) => panic!("unexpected successful decoding"),
	}
	{{- if .HandleDefs }}
	assert_eq!(
		handle_defs.iter().map(get_info_handle_valid).collect::<Vec<_>>(),
		std::iter::repeat(Err(Status::BAD_HANDLE)).take(handle_defs.len()).collect::<Vec<_>>(),
	);
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
	Name, Context, HandleDefs, Value, Bytes, Handles string
}

type decodeSuccessCase struct {
	Name, Context, HandleDefs, ValueType, Value, Bytes, Handles string
}

type encodeFailureCase struct {
	Name, Context, HandleDefs, Value, ErrorCode string
}

type decodeFailureCase struct {
	Name, Context, HandleDefs, ValueType, Bytes, Handles, ErrorCode string
}

// GenerateConformanceTests generates Rust tests.
func GenerateConformanceTests(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
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
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(encodeSuccess.HandleDefs),
				Value:      value,
				Bytes:      buildBytes(encoding.Bytes),
				Handles:    buildHandles(encoding.Handles),
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
		valueType := declName(decl)
		value := visit(decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:       testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(decodeSuccess.HandleDefs),
				ValueType:  valueType,
				Value:      value,
				Bytes:      buildBytes(encoding.Bytes),
				Handles:    buildHandles(encoding.Handles),
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
				Name:       testCaseName(encodeFailure.Name, wireFormat),
				Context:    encodingContext(wireFormat),
				HandleDefs: buildHandleDefs(encodeFailure.HandleDefs),
				Value:      value,
				ErrorCode:  errorCode,
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
				Name:       testCaseName(decodeFailure.Name, encoding.WireFormat),
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(decodeFailure.HandleDefs),
				ValueType:  valueType,
				Bytes:      buildBytes(encoding.Bytes),
				Handles:    buildHandles(encoding.Handles),
				ErrorCode:  errorCode,
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

// Rust errors are defined in src/lib/fidl/rust/fidl/src/error.rs
var rustErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "OutOfRange",
	gidlir.StringNotUtf8:              "Utf8Error",
	gidlir.NonEmptyStringWithNullBody: "UnexpectedNullRef",
	gidlir.StrictUnionFieldNotSet:     "UnknownUnionTag",
	gidlir.StrictUnionUnknownField:    "UnknownUnionTag",
	gidlir.StrictBitsUnknownBit:       "InvalidBitsValue",
	gidlir.StrictEnumUnknownValue:     "InvalidEnumValue",
	gidlir.ExceededMaxOutOfLineDepth:  "MaxRecursionDepth",
	gidlir.InvalidPaddingByte:         "NonZeroPadding",
	gidlir.ExtraHandles:               "ExtraHandles",
}

func rustErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := rustErrorCodeNames[code]; ok {
		return fmt.Sprintf("Error::%s", str), nil
	}
	return "", fmt.Errorf("no rust error string defined for error code %s", code)
}
