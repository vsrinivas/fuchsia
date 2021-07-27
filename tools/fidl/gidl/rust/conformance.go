// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
#![cfg(test)]
#![allow(unused_imports)]

use {
	fidl::{AsHandleRef, Error, Handle, HandleDisposition, HandleInfo, HandleOp, ObjectType, Rights, UnknownData},
	fidl::encoding::{Context, Decodable, Decoder, Encoder, WireFormatVersion},
	fidl_conformance as conformance,
	fuchsia_zircon_status::Status,
	gidl_util::{HandleDef, HandleSubtype, create_handles, copy_handle, copy_handles_at, disown_vec, get_info_handle_valid},
	matches::assert_matches,
};

const _V1_CONTEXT: &Context = &Context { wire_format_version: WireFormatVersion::V1 };
const _V2_CONTEXT: &Context = &Context { wire_format_version: WireFormatVersion::V2 };

{{ range .EncodeSuccessCases }}
{{- if .HandleDefs }}#[cfg(target_os = "fuchsia")]{{ end }}
#[test]
fn test_{{ .Name }}_encode() {
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }});
	let handle_defs = unsafe { disown_vec(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let expected_handles = unsafe { disown_vec(copy_handles_at(handle_defs, &{{ .Handles }})) };
	let expected_handles = expected_handles.as_ref();
	{{- end }}
	let value = &mut {{ .Value }};
	let bytes = &mut Vec::new();
	let handle_dispositions = &mut Vec::new();
	bytes.resize(65536, 0xcd); // fill with junk data
	Encoder::encode_with_context({{ .Context }}, bytes, handle_dispositions, value).unwrap();
	assert_eq!(bytes, &{{ .Bytes }});
	{{- if .HandleDefs }}
	let handles = handle_dispositions.drain(..).map(|h| match h.handle_op {
		HandleOp::Move(hdl) => hdl,
		_ => panic!("unknown handle op"),
	}).collect::<Vec<Handle>>();
	assert_eq!(&handles, expected_handles);
	{{- else }}
	assert!(handle_dispositions.is_empty());
	{{- end }}
}
{{ end }}

{{ range .DecodeSuccessCases }}
{{- if .HandleDefs }}#[cfg(target_os = "fuchsia")]{{ end }}
#[test]
fn test_{{ .Name }}_decode() {
	let bytes = &{{ .Bytes }};
	{{- if .HandleDefs }}
	let handle_definitions = &{{ .HandleDefs }};
	let handle_defs = create_handles(handle_definitions);
	let handle_defs = unsafe { disown_vec(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let mut handles = unsafe { copy_handles_at(handle_defs, &{{ .Handles }}) };
	{{- else }}
	let handle_definitions: Vec<HandleDef> = Vec::new();
	let mut handles = Vec::new();
	{{- end }}
	let mut handle_infos : Vec::<_> = handles.drain(..).zip(handle_definitions.iter()).map(|(h, hd)| {
		HandleInfo {
			handle: h,
			object_type: match hd.subtype {
				HandleSubtype::Event => ObjectType::EVENT,
				HandleSubtype::Channel => ObjectType::CHANNEL,
			},
			rights: hd.rights,
		}
	}).collect();
	let value = &mut {{ .ValueType }}::new_empty();
	Decoder::decode_with_context({{ .Context }}, bytes, &mut handle_infos, value).unwrap();
	{{- if .ForgetHandles }}
	// Forget handles before dropping the expected value, to avoid double closing them.
	struct ForgetHandles({{ .ValueType }});
	impl std::ops::Drop for ForgetHandles {
		#[allow(unused_parens)]
		fn drop(&mut self) {
			{{ .ForgetHandles }}
		}
	}
	let expected_value = ForgetHandles({{ .Value }});
	assert_eq!(value, &expected_value.0);
	{{- else }}
	assert_eq!(value, &{{ .Value }});
	{{- end }}
}
{{ end }}

{{ range .EncodeFailureCases }}
{{- if .HandleDefs }}#[cfg(target_os = "fuchsia")]{{ end }}
#[test]
fn test_{{ .Name }}_encode_failure() {
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }});
	let handle_defs = unsafe { disown_vec(handle_defs) };
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
{{- if .HandleDefs }}#[cfg(target_os = "fuchsia")]{{ end }}
#[test]
fn test_{{ .Name }}_decode_failure() {
	let bytes = &{{ .Bytes }};
	{{- if .HandleDefs }}
	let handle_defs = create_handles(&{{ .HandleDefs }});
	let handle_defs = unsafe { disown_vec(handle_defs) };
	let handle_defs = handle_defs.as_ref();
	let mut handles = unsafe { copy_handles_at(handle_defs, &{{ .Handles }}) };
	{{- else }}
	let mut handles = Vec::new();
	{{- end }}
	let mut handle_infos : Vec::<_> = handles.drain(..).map(|h: fidl::Handle| {
		let info = h.as_handle_ref().basic_info().unwrap();
		HandleInfo {
			handle: h,
			object_type: info.object_type,
			rights: info.rights,
		}
	}).collect();
	let value = &mut {{ .ValueType }}::new_empty();
	match Decoder::decode_with_context({{ .Context }}, bytes, &mut handle_infos, value) {
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
	Name, Context, HandleDefs, ValueType, Value, Bytes, Handles, ForgetHandles string
}

type encodeFailureCase struct {
	Name, Context, HandleDefs, Value, ErrorCode string
}

type decodeFailureCase struct {
	Name, Context, HandleDefs, ValueType, Bytes, Handles, ErrorCode string
}

// GenerateConformanceTests generates Rust tests.
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
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		value := visit(encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupportedForEncode(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(encodeSuccess.HandleDefs),
				Value:      value,
				Bytes:      buildBytes(encoding.Bytes),
				Handles:    buildHandles(gidlir.GetHandlesFromHandleDispositions(encoding.HandleDispositions)),
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
		// Start with "self.0" because this code is placed in a drop(&mut self)
		// function, where self is a wrapper around valueType.
		forgetHandles := buildForgetHandles("self.0", decodeSuccess.Value, decl)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupportedForDecode(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:          testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:       encodingContext(encoding.WireFormat),
				HandleDefs:    buildHandleDefs(decodeSuccess.HandleDefs),
				ValueType:     valueType,
				Value:         value,
				Bytes:         buildBytes(encoding.Bytes),
				Handles:       buildHandles(encoding.Handles),
				ForgetHandles: forgetHandles,
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

		for _, wireFormat := range supportedEncodeWireFormats {
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
			if !wireFormatSupportedForDecode(encoding.WireFormat) {
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
	return fidlgen.ToSnakeCase(fmt.Sprintf("%s_%s", baseName, wireFormat))
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

func encodingContext(wireFormat gidlir.WireFormat) string {
	switch wireFormat {
	case gidlir.V1WireFormat:
		return "_V1_CONTEXT"
	case gidlir.V2WireFormat:
		return "_V2_CONTEXT"
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

// Rust errors are defined in src/lib/fidl/rust/fidl/src/error.rs.
var rustErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.ExceededMaxOutOfLineDepth:    "MaxRecursionDepth",
	gidlir.ExtraHandles:                 "ExtraHandles",
	gidlir.IncorrectHandleType:          "IncorrectHandleSubtype",
	gidlir.InvalidNumBytesInEnvelope:    "InvalidNumBytesInEnvelope",
	gidlir.InvalidNumHandlesInEnvelope:  "InvalidNumHandlesInEnvelope",
	gidlir.InvalidPaddingByte:           "NonZeroPadding",
	gidlir.InvalidPresenceIndicator:     "InvalidPresenceIndicator",
	gidlir.MissingRequiredHandleRights:  "MissingExpectedHandleRights",
	gidlir.NonEmptyStringWithNullBody:   "UnexpectedNullRef",
	gidlir.NonNullableTypeWithNullValue: "UnexpectedNullRef",
	gidlir.NonResourceUnknownHandles:    "CannotStoreUnknownHandles",
	gidlir.StrictBitsUnknownBit:         "InvalidBitsValue",
	gidlir.StrictEnumUnknownValue:       "InvalidEnumValue",
	gidlir.StrictUnionUnknownField:      "UnknownUnionTag",
	gidlir.StringNotUtf8:                "Utf8Error",
	gidlir.StringTooLong:                "OutOfRange",
	gidlir.UnionFieldNotSet:             "UnknownUnionTag",
	gidlir.InvalidInlineBitInEnvelope:   "InvalidInlineBitInEnvelope",
}

func rustErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := rustErrorCodeNames[code]; ok {
		return fmt.Sprintf("Error::%s", str), nil
	}
	return "", fmt.Errorf("no rust error string defined for error code %s", code)
}
