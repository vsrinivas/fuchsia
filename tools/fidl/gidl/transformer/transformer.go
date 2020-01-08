// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package transformer

import (
	"fmt"
	"io"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
#include <gtest/gtest.h>

#include "transformer_conformance_utils.h"

{{ range .TestArrays }}
alignas(8) static uint8_t {{ .Name }}[] = { {{ .Bytes }} };
{{ end }}

{{ range .CodingTables }}
extern "C" const fidl_type_t {{ . }};
{{ end }}

{{ range .SuccessCases }}
TEST(Conformance, {{ .Name }}_Success) {
	ASSERT_TRUE(check_fidl_transform(
		{{ .Transformation }},
		&{{ .FidlType }},
		{{ .SrcBytesVar }}, sizeof {{ .SrcBytesVar }},
		{{ .ExpectedBytesVar }}, sizeof {{ .ExpectedBytesVar }}
	));
}
{{ end }}

{{ range .FailureCases }}
TEST(Conformance, {{ .Name }}_Failure) {
	run_fidl_transform(
		{{ .Transformation }},
		&{{ .FidlType }},
		{{ .SrcBytesVar }}, sizeof {{ .SrcBytesVar }}
	);
}
{{ end }}

`))

type tmplInput struct {
	TestArrays   []testArray
	CodingTables []string
	SuccessCases []successCase
	FailureCases []failureCase
}

type testArray struct {
	Name, Bytes string
}

type successCase struct {
	Name, Transformation, FidlType, SrcBytesVar, ExpectedBytesVar string
}

type failureCase struct {
	Name, Transformation, FidlType, SrcBytesVar string
}

// Generate generates transformer tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	successArrays, successCases, err := successTests(gidl.DecodeSuccess, fidl)
	if err != nil {
		return err
	}
	failureArrays, failureCases, err := failureTests(gidl.DecodeFailure, fidl)
	if err != nil {
		return err
	}

	codingTableNames := make(map[string]bool)
	for _, successCase := range successCases {
		codingTableNames[successCase.FidlType] = true
	}
	for _, failureCase := range failureCases {
		codingTableNames[failureCase.FidlType] = true
	}
	var codingTables []string
	for codingTableName := range codingTableNames {
		codingTables = append(codingTables, codingTableName)
	}

	input := tmplInput{
		TestArrays:   append(successArrays, failureArrays...),
		CodingTables: codingTables,
		SuccessCases: successCases,
		FailureCases: failureCases,
	}
	return tmpl.Execute(wr, input)
}

func successTests(gidlDecodeSuccesses []gidlir.DecodeSuccess, fidl fidlir.Root) ([]testArray, []successCase, error) {
	var arrays []testArray
	var cases []successCase
	for _, decodeSuccess := range gidlDecodeSuccesses {
		typeName := decodeSuccess.Value.(gidlir.Object).Name
		for _, encoding := range decodeSuccess.Encodings {
			srcWireFormat := encoding.WireFormat
			srcBytesVar := testArrayName(
				decodeSuccess.Name, decodeSuccess.Encodings, srcWireFormat)
			expectedBytesVar := testArrayName(
				decodeSuccess.Name, decodeSuccess.Encodings, targetWireFormat(srcWireFormat))
			arrays = append(arrays, testArray{
				Name:  srcBytesVar,
				Bytes: bytesBuilder(encoding.Bytes),
			})
			cases = append(cases, successCase{
				Name:             testCaseName(decodeSuccess.Name, srcWireFormat),
				Transformation:   transformationFrom(srcWireFormat),
				FidlType:         fidlTypeName(srcWireFormat, typeName),
				SrcBytesVar:      srcBytesVar,
				ExpectedBytesVar: expectedBytesVar,
			})
		}
	}
	return arrays, cases, nil
}

func failureTests(gidlDecodeFailures []gidlir.DecodeFailure, fidl fidlir.Root) ([]testArray, []failureCase, error) {
	var arrays []testArray
	var cases []failureCase
	for _, decodeFailure := range gidlDecodeFailures {
		for _, encoding := range decodeFailure.Encodings {
			srcWireFormat := encoding.WireFormat
			srcBytesVar := testArrayName(
				decodeFailure.Name, decodeFailure.Encodings, srcWireFormat)
			arrays = append(arrays, testArray{
				Name:  srcBytesVar,
				Bytes: bytesBuilder(encoding.Bytes),
			})
			cases = append(cases, failureCase{
				Name:           testCaseName(decodeFailure.Name, srcWireFormat),
				Transformation: transformationFrom(srcWireFormat),
				FidlType:       fidlTypeName(srcWireFormat, decodeFailure.Type),
				SrcBytesVar:    srcBytesVar,
			})
		}
	}
	return arrays, cases, nil
}

func targetWireFormat(srcWireFormat gidlir.WireFormat) gidlir.WireFormat {
	switch srcWireFormat {
	case gidlir.OldWireFormat:
		return gidlir.V1WireFormat
	case gidlir.V1WireFormat:
		return gidlir.OldWireFormat
	default:
		panic(fmt.Sprintf("unexpected wire format %v", srcWireFormat))
	}
}

func testArrayName(
	baseName string,
	encodings []gidlir.Encoding,
	srcWireFormat gidlir.WireFormat,
) string {
	var hasSrc, hasTarget bool
	for _, encoding := range encodings {
		if encoding.WireFormat == srcWireFormat {
			hasSrc = true
		} else if encoding.WireFormat == targetWireFormat(srcWireFormat) {
			hasTarget = true
		}
	}
	if !hasSrc && !hasTarget {
		panic(fmt.Sprintf("test %q has no bytes for either wire format", baseName))
	}
	if !(hasSrc && hasTarget) {
		return fidlcommon.ToSnakeCase(fmt.Sprintf("bytes_%s_old_and_v1", baseName))
	}
	switch srcWireFormat {
	case gidlir.OldWireFormat:
		return fidlcommon.ToSnakeCase(fmt.Sprintf("bytes_%s_old", baseName))
	case gidlir.V1WireFormat:
		return fidlcommon.ToSnakeCase(fmt.Sprintf("bytes_%s_v1", baseName))
	default:
		panic(fmt.Sprintf("unexpected wire format %v", srcWireFormat))
	}
}

func testCaseName(baseName string, srcWireFormat gidlir.WireFormat) string {
	switch srcWireFormat {
	case gidlir.OldWireFormat:
		return fidlcommon.ToSnakeCase(fmt.Sprintf("%s_old_to_v1", baseName))
	case gidlir.V1WireFormat:
		return fidlcommon.ToSnakeCase(fmt.Sprintf("%s_v1_to_old", baseName))
	default:
		panic(fmt.Sprintf("unexpected wire format %v", srcWireFormat))
	}
}

func transformationFrom(srcWireFormat gidlir.WireFormat) string {
	switch srcWireFormat {
	case gidlir.OldWireFormat:
		return "FIDL_TRANSFORMATION_OLD_TO_V1"
	case gidlir.V1WireFormat:
		return "FIDL_TRANSFORMATION_V1_TO_OLD"
	default:
		panic(fmt.Sprintf("unexpected wire format %v", srcWireFormat))
	}
}

func fidlTypeName(wireFormat gidlir.WireFormat, typeName string) string {
	switch wireFormat {
	case gidlir.OldWireFormat:
		return fmt.Sprintf("conformance_%sTable", typeName)
	case gidlir.V1WireFormat:
		return fmt.Sprintf("v1_conformance_%sTable", typeName)
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

// TODO(fxb/39685) extract out to common library
func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x", b))
		builder.WriteString(",")
		if i%8 == 7 {
			builder.WriteString("//\n")
		}
	}
	return builder.String()
}
