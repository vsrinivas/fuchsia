// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	_ "embed"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
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
	WireFormatVersion, Name, ValueBuild, ValueVar, Bytes, HandleDefs, Handles string
	FuchsiaOnly, CheckHandleRights                                            bool
}

type decodeSuccessCase struct {
	WireFormatVersion, Name, Type, Bytes, HandleDefs, Handles, EqualityCheck, HandleKoidVectorName string
	FuchsiaOnly                                                                                    bool
}

type encodeFailureCase struct {
	WireFormatVersion, Name, ValueBuild, ValueVar, HandleDefs string

	FuchsiaOnly bool
}

type decodeFailureCase struct {
	WireFormatVersion, Name, Type, Bytes, HandleDefs, Handles string
	FuchsiaOnly                                               bool
}

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
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
		DecodeSuccessCases: decodeSuccessCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
	return buf.Bytes(), err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		// TODO(fxbug.dev/111709): Translate this to GIDL denylist, or properly support the test case.
		if gidlir.ContainsUnknownField(encodeSuccess.Value) {
			continue
		}
		valueBuild, valueVar := BuildValue(encodeSuccess.Value, decl, HandleReprRaw)
		fuchsiaOnly := decl.IsResourceType() || len(encodeSuccess.HandleDefs) > 0
		for _, encoding := range encodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:              testCaseName(encodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:        buildHandleDefs(encodeSuccess.HandleDefs),
				Handles:           libhlcpp.BuildRawHandleDispositions(encoding.HandleDispositions),
				FuchsiaOnly:       fuchsiaOnly,
				CheckHandleRights: encodeSuccess.CheckHandleRights,
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
			return nil, fmt.Errorf("encode success %s: %s", decodeSuccess.Name, err)
		}
		actualValueVar := "value"
		handleKoidVectorName := "handle_koids"
		equalityCheck := buildEqualityCheck(actualValueVar, decodeSuccess.Value, decl, handleKoidVectorName)
		fuchsiaOnly := decl.IsResourceType() || len(decodeSuccess.HandleDefs) > 0
		for _, encoding := range decodeSuccess.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			decodeSuccessCases = append(decodeSuccessCases, decodeSuccessCase{
				Name:                 testCaseName(decodeSuccess.Name, encoding.WireFormat),
				WireFormatVersion:    wireFormatVersionName(encoding.WireFormat),
				Type:                 conformanceType(gidlir.TypeFromValue(decodeSuccess.Value)),
				Bytes:                libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:           buildHandleInfoDefs(decodeSuccess.HandleDefs),
				Handles:              libhlcpp.BuildRawHandleInfos(encoding.Handles),
				EqualityCheck:        equalityCheck,
				HandleKoidVectorName: handleKoidVectorName,
				FuchsiaOnly:          fuchsiaOnly,
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
		// TODO(fxbug.dev/111709): Translate this to GIDL denylist, or properly support the test case.
		if gidlir.ContainsUnknownField(encodeFailure.Value) {
			continue
		}
		valueBuild, valueVar := BuildValue(encodeFailure.Value, decl, HandleReprRaw)
		fuchsiaOnly := decl.IsResourceType() || len(encodeFailure.HandleDefs) > 0
		for _, wireFormat := range supportedWireFormats {
			encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
				Name:              testCaseName(encodeFailure.Name, wireFormat),
				WireFormatVersion: wireFormatVersionName(wireFormat),
				ValueBuild:        valueBuild,
				ValueVar:          valueVar,
				HandleDefs:        buildHandleDefs(encodeFailure.HandleDefs),
				FuchsiaOnly:       fuchsiaOnly,
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
		for _, encoding := range decodeFailure.Encodings {
			if !wireFormatSupported(encoding.WireFormat) {
				continue
			}
			fuchsiaOnly := decl.IsResourceType() || len(decodeFailure.HandleDefs) > 0
			decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
				Name:              testCaseName(decodeFailure.Name, encoding.WireFormat),
				WireFormatVersion: wireFormatVersionName(encoding.WireFormat),
				Type:              conformanceType(decodeFailure.Type),
				Bytes:             libhlcpp.BuildBytes(encoding.Bytes),
				HandleDefs:        buildHandleInfoDefs(decodeFailure.HandleDefs),
				Handles:           libhlcpp.BuildRawHandleInfos(encoding.Handles),
				FuchsiaOnly:       fuchsiaOnly,
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

func wireFormatVersionName(wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("::fidl::internal::WireFormatVersion::k%s", fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func conformanceType(gidlTypeString string) string {
	// Note: only works for domain objects (not protocols & services)
	return "test_conformance::" + fidlgen.ToUpperCamelCase(gidlTypeString)
}

func testCaseName(baseName string, wireFormat gidlir.WireFormat) string {
	return fmt.Sprintf("%s_%s", baseName,
		fidlgen.ToUpperCamelCase(wireFormat.String()))
}

func buildHandleDef(def gidlir.HandleDef) string {
	switch def.Subtype {
	case fidlgen.HandleSubtypeChannel:
		return fmt.Sprintf("conformance_utils::CreateChannel(%d)", def.Rights)
	case fidlgen.HandleSubtypeEvent:
		return fmt.Sprintf("conformance_utils::CreateEvent(%d)", def.Rights)
	default:
		panic(fmt.Sprintf("unsupported handle subtype: %s", def.Subtype))
	}
}

func handleType(subtype fidlgen.HandleSubtype) string {
	switch subtype {
	case fidlgen.HandleSubtypeChannel:
		return "ZX_OBJ_TYPE_CHANNEL"
	case fidlgen.HandleSubtypeEvent:
		return "ZX_OBJ_TYPE_EVENT"
	default:
		panic(fmt.Sprintf("unsupported handle subtype: %s", subtype))
	}
}

func buildHandleDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("std::vector<zx_handle_t>{\n")
	for i, d := range defs {
		builder.WriteString(buildHandleDef(d))
		// Write indices corresponding to the .gidl file handle_defs block.
		builder.WriteString(fmt.Sprintf(", // #%d\n", i))
	}
	builder.WriteString("}")
	return builder.String()
}

func buildHandleInfoDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("std::vector<zx_handle_info_t>{\n")
	for i, d := range defs {
		builder.WriteString(fmt.Sprintf(`
// #%d
zx_handle_info_t{
	.handle = %s,
	.type = %s,
	.rights = %d,
	.unused = 0u,
},
`, i, buildHandleDef(d), handleType(d.Subtype), d.Rights))
	}
	builder.WriteString("}")
	return builder.String()
}
