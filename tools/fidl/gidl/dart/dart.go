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
	schema := gidlmixer.BuildSchema(fidl)
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, schema)
	if err != nil {
		return err
	}
	decodeSuccessCases, err := decodeSuccessCases(gidl.DecodeSuccess, schema)
	if err != nil {
		return err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.EncodeFailure, schema)
	if err != nil {
		return err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.DecodeFailure, schema)
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

func typeName(decl gidlmixer.NamedDeclaration) string {
	parts := strings.Split(decl.Name(), "/")
	lastPart := parts[len(parts)-1]
	return dartTypeName(lastPart)
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

func visit(value interface{}, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool:
		return strconv.FormatBool(value)
	case int64, uint64, float64:
		switch decl := decl.(type) {
		case *gidlmixer.IntegerDecl, *gidlmixer.FloatDecl:
			return fmt.Sprintf("%#v", value)
		case gidlmixer.NamedDeclaration:
			return fmt.Sprintf("%s.ctor(%#v)", typeName(decl), value)
		}
	case string:
		return fidlcommon.SingleQuote(value)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return onRecord(value, decl)
		case *gidlmixer.TableDecl:
			return onRecord(value, decl)
		case *gidlmixer.UnionDecl:
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
	panic(fmt.Sprintf("not implemented: %T", value))
}

func onRecord(value gidlir.Record, decl gidlmixer.RecordDeclaration) string {
	var args []string
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		val := visit(field.Value, fieldDecl)
		args = append(args, fmt.Sprintf("%s: %s", fidlcommon.ToLowerCamelCase(field.Key.Name), val))
	}
	return fmt.Sprintf("%s(%s)", fidlcommon.ToUpperCamelCase(value.Name), strings.Join(args, ", "))
}

func onUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) string {
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		val := visit(field.Value, fieldDecl)
		return fmt.Sprintf("%s.with%s(%s)", value.Name, fidlcommon.ToUpperCamelCase(field.Key.Name), val)
	}
	// Not currently possible to construct a union in dart with an invalid value.
	panic("unions must have a value set")
}

func onList(value []interface{}, decl gidlmixer.ListDeclaration) string {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elements = append(elements, visit(item, elemDecl))
	}
	if integerDecl, ok := elemDecl.(*gidlmixer.IntegerDecl); ok {
		typeName := fidlcommon.ToUpperCamelCase(string(integerDecl.Subtype()))
		return fmt.Sprintf("%sList.fromList([%s])", typeName, strings.Join(elements, ", "))
	}
	return fmt.Sprintf("[%s]", strings.Join(elements, ", "))
}

// Dart error codes defined in: topaz/public/dart/fidl/lib/src/error.dart.
var dartErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "fidlStringTooLong",
	gidlir.StringNotUtf8:              "unknown",
	gidlir.NonEmptyStringWithNullBody: "fidlNonNullableTypeWithNullValue",
	gidlir.StrictUnionFieldNotSet:     "fidlStrictXUnionFieldNotSet",
	gidlir.StrictUnionUnknownField:    "fidlStrictXUnionUnknownField",
}

func dartErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := dartErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.FidlErrorCode.%s", str), nil
	}
	return "", fmt.Errorf("no dart error string defined for error code %s", code)
}
