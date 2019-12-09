// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dart

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:fidl/fidl.dart' as fidl;

import 'conformance_test_types.dart';
import 'gidl.dart';

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
				{{ .Value }},
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
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
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
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure)
	if err != nil {
		return err
	}
	return tmpl.Execute(wr, tmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, fidl fidlir.Root) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(encodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		valueStr := visit(encodeSuccess.Value, decl)
		valueType := typeName(decl.(*gidlmixer.StructDecl))
		for _, encoding := range encodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
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

func decodeSuccessCases(gidlDecodeSuccesses []gidlir.DecodeSuccess, fidl fidlir.Root) ([]decodeSuccessCase, error) {
	var decodeSuccessCases []decodeSuccessCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(decodeSuccess.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("decode success %s: %s", decodeSuccess.Name, err)
		}
		if gidlir.ContainsUnknownField(decodeSuccess.Value) {
			continue
		}
		valueStr := visit(decodeSuccess.Value, decl)
		valueType := typeName(decl.(*gidlmixer.StructDecl))
		for _, encoding := range decodeSuccess.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
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

func encodeFailureCases(gidlEncodeFailures []gidlir.EncodeFailure, fidl fidlir.Root) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailures {
		decl, err := gidlmixer.ExtractDeclarationUnsafe(encodeFailure.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		if gidlir.ContainsUnknownField(encodeFailure.Value) {
			continue
		}
		errorCode, err := dartErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, err
		}
		valueStr := visit(encodeFailure.Value, decl)
		valueType := typeName(decl.(*gidlmixer.StructDecl))
		for _, wireFormat := range encodeFailure.WireFormats {
			if wireFormat == gidlir.OldWireFormat {
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

func decodeFailureCases(gidlDecodeFailures []gidlir.DecodeFailure) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		errorCode, err := dartErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, err
		}
		valueType := dartTypeName(decodeFailure.Type)
		for _, encoding := range decodeFailure.Encodings {
			if encoding.WireFormat == gidlir.OldWireFormat {
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

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fidlcommon.SingleQuote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}

func encoderName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("Encoders.%s", wireFormat)
}

func decoderName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("Decoders.%s", wireFormat)
}

func typeName(decl *gidlmixer.StructDecl) string {
	parts := strings.Split(string(decl.Name), "/")
	lastPart := parts[len(parts)-1]
	return dartTypeName(lastPart)
}

func dartTypeName(inputType string) string {
	return fmt.Sprintf("k%s_Type", inputType)
}

func bytesBuilder(bytes []byte) string {
	var sb strings.Builder
	sb.WriteString("Uint8List.fromList([\n")
	for i, b := range bytes {
		sb.WriteString(fmt.Sprintf("0x%02x", b))
		sb.WriteString(",")
		if i%8 == 7 {
			// Note: empty comments are written to preserve formatting. See:
			// https://github.com/dart-lang/dart_style/wiki/FAQ#why-does-the-formatter-mess-up-my-collection-literals
			sb.WriteString(" //\n")
		}
	}
	sb.WriteString("])")
	return sb.String()
}

func visit(value interface{}, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool:
		return strconv.FormatBool(value)
	case int64:
		return fmt.Sprintf("%d", value)
	case uint64:
		return fmt.Sprintf("0x%x", value)
	case float64:
		return strconv.FormatFloat(value, 'g', -1, 64)
	case string:
		return fidlcommon.SingleQuote(value)
	case gidlir.Object:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return onObject(value, decl)
		case *gidlmixer.TableDecl:
			return onObject(value, decl)
		case *gidlmixer.UnionDecl:
			return onUnion(value, decl)
		case *gidlmixer.XUnionDecl:
			return onUnion(value, decl)
		}
	case []interface{}:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return onList(value, decl)
		case *gidlmixer.VectorDecl:
			return onList(value, decl)
		}
	case nil:
		if !decl.IsNullable() {
			panic(fmt.Sprintf("got nil for non-nullable type: %T", decl))
		}
		return "null"

	}
	panic(fmt.Sprintf("unexpected value visited %v (decl %v)", value, decl))
}

func onObject(value gidlir.Object, decl gidlmixer.KeyedDeclaration) string {
	var args []string
	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		fieldDecl, _ := decl.ForKey(field.Key)
		val := visit(field.Value, fieldDecl)
		args = append(args, fmt.Sprintf("%s: %s", fidlcommon.ToLowerCamelCase(field.Key.Name), val))
	}
	return fmt.Sprintf("%s(%s)", fidlcommon.ToUpperCamelCase(value.Name), strings.Join(args, ", "))
}

func onUnion(value gidlir.Object, decl gidlmixer.KeyedDeclaration) string {
	for _, field := range value.Fields {
		if field.Key.Name == "" {
			panic("unknown field not supported")
		}
		fieldDecl, _ := decl.ForKey(field.Key)
		val := visit(field.Value, fieldDecl)
		return fmt.Sprintf("%s.with%s(%s)", value.Name, fidlcommon.ToUpperCamelCase(field.Key.Name), val)
	}
	// Not currently possible to construct a union/xunion in dart with an invalid value.
	panic("unions must have a value set")
}

func onList(value []interface{}, decl gidlmixer.ListDeclaration) string {
	var elements []string
	elemDecl, _ := decl.Elem()
	for _, item := range value {
		elements = append(elements, visit(item, elemDecl))
	}
	if numberDecl, ok := elemDecl.(*gidlmixer.NumberDecl); ok {
		return fmt.Sprintf("%sList.fromList([%s])", fidlcommon.ToUpperCamelCase(string(numberDecl.Typ)), strings.Join(elements, ", "))
	}
	return fmt.Sprintf("[%s]", strings.Join(elements, ", "))
}

// Dart error codes defined in: topaz/public/dart/fidl/lib/src/error.dart.
var dartErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "fidlStringTooLong",
	gidlir.NonEmptyStringWithNullBody: "fidlNonNullableTypeWithNullValue",
	gidlir.StrictXUnionFieldNotSet:    "fidlStrictXUnionFieldNotSet",
	gidlir.StrictXUnionUnknownField:   "fidlStrictXUnionUnknownField",
}

func dartErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := dartErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.FidlErrorCode.%s", str), nil
	}
	return "", fmt.Errorf("no dart error string defined for error code %s", code)
}
