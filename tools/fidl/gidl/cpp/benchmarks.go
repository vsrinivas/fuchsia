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

{{ range .Benchmarks }}
{{ .Type }} Build{{ .Name }}() {
  {{ .ValueBuild }}
  auto result = {{ .ValueVar }};
  return result;
}
bool BenchmarkBuilder{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }});
}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }});
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }});
}
{{ end }}

void RegisterTests() {
  {{ range .Benchmarks }}
  perftest::RegisterTest("HLCPP/Builder/{{ .Path }}/WallTime", BenchmarkBuilder{{ .Name }});
  perftest::RegisterTest("HLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
  perftest::RegisterTest("HLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
  {{ end }}
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
`))

type benchmarksTmplInput struct {
	Benchmarks []benchmark
}
type benchmark struct {
	Path, Name, Type     string
	ValueBuild, ValueVar string
}

// Generate generates High-Level C++ benchmarks.
func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value)
		if err != nil {
			return fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(gidlBenchmark.Value, decl)
		valueBuild := valueBuilder.String()
		benchmarks = append(benchmarks, benchmark{
			Path:       gidlBenchmark.Name,
			Name:       benchmarkName(gidlBenchmark.Name),
			Type:       benchmarkTypeFromValue(gidlBenchmark.Value),
			ValueBuild: valueBuild,
			ValueVar:   valueVar,
		})
	}
	return benchmarksTmpl.Execute(wr, benchmarksTmplInput{
		Benchmarks: benchmarks,
	})
}

func benchmarkTypeFromValue(value gidlir.Value) string {
	return fmt.Sprintf("benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
