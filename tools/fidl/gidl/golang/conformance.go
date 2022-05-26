// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"bytes"
	_ "embed"
	"fmt"
	"strconv"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
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
	Name, Context, Value, Bytes, HandleDefs, Handles string
	CheckRights                                      bool
}

type decodeSuccessCase struct {
	Name, Context, Type, Value, Bytes, HandleDefs, Handles          string
	EqualityCheck, EqualityCheckInputVar, EqualityCheckKoidArrayVar string
}

type encodeFailureCase struct {
	Name, Context, Value, ErrorCode, HandleDefs string
}

type decodeFailureCase struct {
	Name, Context, ValueType, Bytes, ErrorCode, HandleDefs, Handles string
}

// GenerateConformanceTests generates Go tests.
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
	err = withGoFmt{conformanceTmpl}.Execute(&buf, input)
	return buf.Bytes(), err
}

func marshalerContext(wireFormat gidlir.WireFormat) string {
	switch wireFormat {
	case gidlir.V1WireFormat:
		return `fidl.MarshalerContext{UseV2WireFormat: false}`
	case gidlir.V2WireFormat:
		return `fidl.MarshalerContext{UseV2WireFormat: true}`
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
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
				Name:        testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:     marshalerContext(encoding.WireFormat),
				Value:       value,
				Bytes:       buildBytes(encoding.Bytes),
				HandleDefs:  buildHandleDefs(encodeSuccess.HandleDefs),
				Handles:     buildHandleDispositions(encoding.HandleDispositions),
				CheckRights: encodeSuccess.CheckHandleRights,
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
		value := visit(decodeSuccess.Value, decl)
		equalityCheckInputVar := "val"
		equalityCheckKoidArrayVar := "koidArray"
		equalityCheck := BuildEqualityCheck(equalityCheckInputVar, decodeSuccess.Value, decl, equalityCheckKoidArrayVar)
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                      testCaseName(decodeSuccess.Name, encoding.WireFormat),
				Context:                   marshalerContext(encoding.WireFormat),
				Value:                     value,
				Bytes:                     buildBytes(encoding.Bytes),
				Type:                      declName(decl),
				HandleDefs:                buildHandleDefs(decodeSuccess.HandleDefs),
				Handles:                   buildHandleInfos(encoding.Handles),
				EqualityCheck:             equalityCheck,
				EqualityCheckInputVar:     equalityCheckInputVar,
				EqualityCheckKoidArrayVar: equalityCheckKoidArrayVar,
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
		code, err := goErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, fmt.Errorf("encode failure %s: %s", encodeFailure.Name, err)
		}
		value := visit(encodeFailure.Value, decl)
		for _, wireFormat := range supportedWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:       testCaseName(encodeFailure.Name, wireFormat),
				Context:    marshalerContext(wireFormat),
				Value:      value,
				ErrorCode:  code,
				HandleDefs: buildHandleDefs(encodeFailure.HandleDefs),
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
		code, err := goErrorCode(decodeFailure.Err)
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
				Context:    marshalerContext(encoding.WireFormat),
				ValueType:  valueType,
				Bytes:      buildBytes(encoding.Bytes),
				ErrorCode:  code,
				HandleDefs: buildHandleDefs(decodeFailure.HandleDefs),
				Handles:    buildHandleInfos(encoding.Handles),
			})
		}
	}
	return decodeFailureCases, nil
}

var supportedWireFormats = []gidlir.WireFormat{
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

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return strconv.Quote(fmt.Sprintf("%s_%s", baseName, wireFormat))
}
