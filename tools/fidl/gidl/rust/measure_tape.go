// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	_ "embed"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var (
	//go:embed measure_tape.tmpl
	measureTapeTmplText string

	measureTapeTmpl = template.Must(template.New("measureTapeTmpl").Parse(measureTapeTmplText))
)

const measureTapeWireFormat = gidlir.V2WireFormat

type measureTapeTmplInput struct {
	MeasureTapeTestCases []measureTapeTestCase
}

type measureTapeTestCase struct {
	Name, Context, HandleDefs, Value string
	NumBytes, NumHandles             int
}

func GenerateMeasureTapeTests(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	var tmplInput measureTapeTmplInput
	schema := gidlmixer.BuildSchema(fidl)
	allowedTypes := map[string]struct{}{}
	for _, name := range config.FilterTypes {
		if !strings.HasPrefix(name, "test.conformance/") {
			return nil, fmt.Errorf("invalid filter type: %s", name)
		}
		typeName := strings.TrimPrefix(name, "test.conformance/")
		allowedTypes[typeName] = struct{}{}
	}
	for _, encodeSuccess := range gidl.EncodeSuccess {
		valueTypeName := gidlir.TypeFromValue(encodeSuccess.Value)
		if _, ok := allowedTypes[valueTypeName]; !ok {
			continue
		}
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		value := visit(encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			if encoding.WireFormat != measureTapeWireFormat {
				continue
			}
			tmplInput.MeasureTapeTestCases = append(tmplInput.MeasureTapeTestCases, measureTapeTestCase{
				Name:       testCaseName(encodeSuccess.Name, encoding.WireFormat),
				Context:    encodingContext(encoding.WireFormat),
				HandleDefs: buildHandleDefs(encodeSuccess.HandleDefs),
				Value:      value,
				NumBytes:   len(encoding.Bytes),
				NumHandles: len(encoding.HandleDispositions),
			})
		}
	}
	var buf bytes.Buffer
	err := measureTapeTmpl.Execute(&buf, tmplInput)
	return buf.Bytes(), err
}
