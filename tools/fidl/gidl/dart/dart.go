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
			{{.Name}},
			{{.Value}},
			{{.ValueType}},
			{{.Bytes}});
		{{ end }}
			});

	group('decode success cases', () {
		{{ range .DecodeSuccessCases }}
		DecodeSuccessCase.run(
			{{.Name}},
			{{.Value}},
			{{.ValueType}},
			{{.Bytes}});
		{{ end }}
			});

		group('encode failure cases', () {
	{{ range .EncodeFailureCases }}
	EncodeFailureCase.run(
		{{.Name}},
		{{.Value}},
		{{.ValueType}},
		{{.ErrorCode}});
	{{ end }}
		});

		group('decode failure cases', () {
	{{ range .DecodeFailureCases }}
	DecodeFailureCase.run(
		{{.Name}},
		{{.ValueType}},
		{{.Bytes}},
		{{.ErrorCode}});
	{{ end }}
		});
	});


}
`))

type tmplInput struct {
	EncodeSuccessCases       []encodeSuccessCase
	DecodeSuccessCases       []decodeSuccessCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type encodeSuccessCase struct {
	Name, Value, ValueType, Bytes string
}

type decodeSuccessCase struct {
	Name, Value, ValueType, Bytes string
}

type encodeFailureCase struct {
	Name, Value, ValueType, ErrorCode string
}

type decodeFailureCase struct {
	Name, ValueType, Bytes, ErrorCode string
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
		valueStr := visit(encodeSuccess.Value, decl)
		encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
			Name:      fidlcommon.SingleQuote(encodeSuccess.Name),
			Value:     valueStr,
			ValueType: typeName(decl.(*gidlmixer.StructDecl)),
			Bytes:     bytesBuilder(encodeSuccess.Bytes),
		})
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
		valueStr := visit(decodeSuccess.Value, decl)
		decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
			Name:      fidlcommon.SingleQuote(decodeSuccess.Name),
			Value:     valueStr,
			ValueType: typeName(decl.(*gidlmixer.StructDecl)),
			Bytes:     bytesBuilder(decodeSuccess.Bytes),
		})
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
		valueStr := visit(encodeFailure.Value, decl)
		errorCode, err := dartErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, err
		}
		encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
			Name:      fidlcommon.SingleQuote(encodeFailure.Name),
			Value:     valueStr,
			ValueType: typeName(decl.(*gidlmixer.StructDecl)),
			ErrorCode: errorCode,
		})
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
		decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
			Name:      fidlcommon.SingleQuote(decodeFailure.Name),
			ValueType: dartTypeName(decodeFailure.Type),
			Bytes:     bytesBuilder(decodeFailure.Bytes),
			ErrorCode: errorCode,
		})
	}
	return decodeFailureCases, nil
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
		return fmt.Sprintf("0x%x", value)
	case uint64:
		return fmt.Sprintf("0x%x", value)
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
	}
	panic(fmt.Sprintf("unexpected value visited %v (decl %v)", value, decl))
}

func onObject(value gidlir.Object, decl gidlmixer.KeyedDeclaration) string {
	var args []string
	for _, field := range value.Fields {
		fieldDecl, _ := decl.ForKey(field.Name)
		val := visit(field.Value, fieldDecl)
		args = append(args, fmt.Sprintf("%s: %s", fidlcommon.ToLowerCamelCase(field.Name), val))
	}
	return fmt.Sprintf("%s(%s)", value.Name, strings.Join(args, ", "))
}

func onUnion(value gidlir.Object, decl gidlmixer.KeyedDeclaration) string {
	for _, field := range value.Fields {
		fieldDecl, _ := decl.ForKey(field.Name)
		val := visit(field.Value, fieldDecl)
		return fmt.Sprintf("%s.with%s(%s)", value.Name, strings.Title(field.Name), val)
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
	gidlir.StringTooLong:               "fidlStringTooLong",
	gidlir.NullEmptyStringWithNullBody: "fidlNonNullableTypeWithNullValue",
	gidlir.StrictXUnionFieldNotSet:     "fidlStrictXUnionFieldNotSet",
	gidlir.StrictXUnionUnknownField:    "fidlStrictXUnionUnknownField",
}

func dartErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := dartErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.FidlErrorCode.%s", str), nil
	}
	return "", fmt.Errorf("no dart error string defined for error code %s", code)
}
