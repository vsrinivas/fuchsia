// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	fidlir "fidl/compiler/backend/types"
	"fmt"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
	"io"
	"strings"
	"text/template"
)

var benchmarksTmpl = template.Must(template.New("tmpl").Parse(`
#include <benchmarkfidl/cpp/fidl.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/hlcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/encode_benchmark_util.h"

namespace {

{{ range .BuilderBenchmarks }}
{{ .Type }} Build{{ .Name }}() {
  {{ .ValueBuild }}
  return {{ .ValueVar }};
}
bool BenchmarkBuilder{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }});
}
{{ end }}

{{ range .EncodeBenchmarks }}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }}());
}
{{ end }}

{{ range .DecodeBenchmarks }}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }}());
}
{{ end }}

void RegisterTests() {
  {{ range .BuilderBenchmarks }}
  perftest::RegisterTest("HLCPP/Builder/{{ .Path }}/WallTime", BenchmarkBuilder{{ .Name }});
  {{ end }}

  {{ range .EncodeBenchmarks }}
  perftest::RegisterTest("HLCPP/Encode/{{ .Path }}/WallTime", BenchmarkEncode{{ .Name }});
  {{ end }}

  {{ range .DecodeBenchmarks }}
  perftest::RegisterTest("HLCPP/Decode/{{ .Path }}/WallTime", BenchmarkDecode{{ .Name }});
  {{ end }}
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
`))

type benchmarksTmplInput struct {
	BuilderBenchmarks []builderBenchmark
	EncodeBenchmarks  []encodeBenchmark
	DecodeBenchmarks  []decodeBenchmark
}
type builderBenchmark struct {
	Path, Name, Type     string
	ValueBuild, ValueVar string
}

type encodeBenchmark struct {
	Path, Name string
}

type decodeBenchmark struct {
	Path, Name string
}

// Generate generates High-Level C++ benchmarks.
func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	schema := gidlmixer.BuildSchema(fidl)
	builderBenchmarks, err := builderBenchmarks(gidl.EncodeBenchmark, schema)
	if err != nil {
		return err
	}
	encodeBenchmarks, err := encodeBenchmarks(gidl.EncodeBenchmark, schema)
	if err != nil {
		return err
	}
	decodeBenchmarks, err := decodeBenchmarks(gidl.DecodeBenchmark, schema)
	if err != nil {
		return err
	}
	return benchmarksTmpl.Execute(wr, benchmarksTmplInput{
		BuilderBenchmarks: builderBenchmarks,
		EncodeBenchmarks:  encodeBenchmarks,
		DecodeBenchmarks:  decodeBenchmarks,
	})
}
func builderBenchmarks(gidlEncodeBenchmarks []gidlir.EncodeBenchmark, schema gidlmixer.Schema) ([]builderBenchmark, error) {
	var builderBenchmarks []builderBenchmark
	for _, gidlEncodeBenchmark := range gidlEncodeBenchmarks {
		decl, err := schema.ExtractDeclaration(gidlEncodeBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("builder benchmark %s: %s", gidlEncodeBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlEncodeBenchmark.Value) {
			continue
		}
		valueBuilder := newCppValueBuilder()
		gidlmixer.Visit(&valueBuilder, gidlEncodeBenchmark.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		builderBenchmarks = append(builderBenchmarks, builderBenchmark{
			Path:       gidlEncodeBenchmark.Name,
			Name:       benchmarkName(gidlEncodeBenchmark.Name),
			Type:       benchmarkTypeFromValue(gidlEncodeBenchmark.Value),
			ValueBuild: valueBuild,
			ValueVar:   valueVar,
		})
	}
	return builderBenchmarks, nil
}

func encodeBenchmarks(gidlEncodeBenchmarks []gidlir.EncodeBenchmark, schema gidlmixer.Schema) ([]encodeBenchmark, error) {
	var encodeBenchmarks []encodeBenchmark
	for _, gidlEncodeBenchmark := range gidlEncodeBenchmarks {
		encodeBenchmarks = append(encodeBenchmarks, encodeBenchmark{
			Path: gidlEncodeBenchmark.Name,
			Name: benchmarkName(gidlEncodeBenchmark.Name),
		})
	}
	return encodeBenchmarks, nil
}

func decodeBenchmarks(gidlDecodeBenchmarks []gidlir.DecodeBenchmark, schema gidlmixer.Schema) ([]decodeBenchmark, error) {
	var decodeBenchmarks []decodeBenchmark
	for _, gidlDecodeBenchmark := range gidlDecodeBenchmarks {
		decodeBenchmarks = append(decodeBenchmarks, decodeBenchmark{
			Path: gidlDecodeBenchmark.Name,
			Name: benchmarkName(gidlDecodeBenchmark.Name),
		})
	}
	return decodeBenchmarks, nil
}

func benchmarkTypeFromValue(value gidlir.Value) string {
	return fmt.Sprintf("benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
