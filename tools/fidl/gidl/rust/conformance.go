// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	_ "embed"
	"fmt"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidllibrust "go.fuchsia.dev/fuchsia/tools/fidl/gidl/librust"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var (
	//go:embed conformance.tmpl
	conformanceTmplText string

	conformanceTmpl = template.Must(template.New("conformanceTmpl").Parse(conformanceTmplText))
)

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
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(encodeSuccess.HandleDefs),
				Value:      value,
				Bytes:      gidllibrust.BuildBytes(encoding.Bytes),
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
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:          testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:       encodingContext(encoding.WireFormat),
				HandleDefs:    buildHandleDefs(decodeSuccess.HandleDefs),
				ValueType:     valueType,
				Value:         value,
				Bytes:         gidllibrust.BuildBytes(encoding.Bytes),
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

		for _, wireFormat := range supportedWireFormats {
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
				Bytes:      gidllibrust.BuildBytes(encoding.Bytes),
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

var supportedWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
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
	gidlir.CountExceedsLimit:                  "OutOfRange",
	gidlir.EnvelopeBytesExceedMessageLength:   "InvalidNumBytesInEnvelope",
	gidlir.EnvelopeHandlesExceedMessageLength: "InvalidNumHandlesInEnvelope",
	gidlir.ExceededMaxOutOfLineDepth:          "MaxRecursionDepth",
	gidlir.IncorrectHandleType:                "IncorrectHandleSubtype",
	gidlir.InvalidBoolean:                     "InvalidBoolean",
	gidlir.InvalidEmptyStruct:                 "Invalid",
	gidlir.InvalidInlineBitInEnvelope:         "InvalidInlineBitInEnvelope",
	gidlir.InvalidInlineMarkerInEnvelope:      "InvalidInlineMarkerInEnvelope",
	gidlir.InvalidNumBytesInEnvelope:          "InvalidNumBytesInEnvelope",
	gidlir.InvalidNumHandlesInEnvelope:        "InvalidNumHandlesInEnvelope",
	gidlir.InvalidPaddingByte:                 "NonZeroPadding",
	gidlir.InvalidPresenceIndicator:           "InvalidPresenceIndicator",
	gidlir.InvalidHandlePresenceIndicator:     "InvalidPresenceIndicator",
	gidlir.MissingRequiredHandleRights:        "MissingExpectedHandleRights",
	gidlir.NonEmptyStringWithNullBody:         "UnexpectedNullRef",
	gidlir.NonEmptyVectorWithNullBody:         "UnexpectedNullRef",
	gidlir.NonNullableTypeWithNullValue:       "NotNullable",
	gidlir.NonResourceUnknownHandles:          "CannotStoreUnknownHandles",
	gidlir.StrictBitsUnknownBit:               "InvalidBitsValue",
	gidlir.StrictEnumUnknownValue:             "InvalidEnumValue",
	gidlir.StrictUnionUnknownField:            "UnknownUnionTag",
	gidlir.StringCountExceeds32BitLimit:       "OutOfRange",
	gidlir.StringNotUtf8:                      "Utf8Error",
	gidlir.StringTooLong:                      "OutOfRange",
	gidlir.TableCountExceeds32BitLimit:        "OutOfRange",
	gidlir.TooFewBytes:                        "OutOfRange",
	gidlir.TooFewBytesInPrimaryObject:         "OutOfRange",
	gidlir.TooFewHandles:                      "OutOfRange",
	gidlir.TooManyBytesInMessage:              "ExtraBytes",
	gidlir.TooManyHandlesInMessage:            "ExtraHandles",
	gidlir.UnionFieldNotSet:                   "UnknownUnionTag",
	gidlir.UnexpectedOrdinal:                  "OutOfRange",
	gidlir.VectorCountExceeds32BitLimit:       "OutOfRange",
}

func rustErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := rustErrorCodeNames[code]; ok {
		return fmt.Sprintf("Error::%s", str), nil
	}
	return "", fmt.Errorf("no rust error string defined for error code %s", code)
}
