// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var benchmarkTmpl = template.Must(template.New("tmpl").Parse(`
#include <{{ .FidlInclude }}>
#include <cts/tests/pkg/fidl/cpp/test/handle_util.h>
#include <perftest/perftest.h>

#include <vector>

#include "src/tests/benchmarks/fidl/cpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/cpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/cpp/encode_benchmark_util.h"

namespace {

{{ range .Benchmarks }}
{{- if .HandleDefs }}
std::vector<zx_handle_t> BuildHandles{{ .Name }}() {
	return {{ .HandleDefs }};
}

{{ .Type }} BuildFromHandles{{ .Name }}(const std::vector<zx_handle_t>& handle_defs) {
  {{ .ValueBuild }}
  auto result =  {{ .ValueVar }};
  return result;
}

{{ .Type }} Build{{ .Name }}() {
  return BuildFromHandles{{ .Name }}(BuildHandles{{ .Name }}());
}
{{- else }}
std::tuple<> BuildEmptyContext{{ .Name }}() {
	return std::make_tuple();
}

{{ .Type }} BuildFromEmptyContext{{ .Name }}(std::tuple<> _context) {
	{{ .ValueBuild }}
	auto result = {{ .ValueVar }};
	return result;
}

{{ .Type }} Build{{ .Name }}() {
  {{ .ValueBuild }}
  auto result = {{ .ValueVar }};
  return result;
}
{{- end }}

bool BenchmarkBuilder{{ .Name }}(perftest::RepeatState* state) {
{{- if .HandleDefs }}
  return cpp_benchmarks::BuilderBenchmark(state, BuildFromHandles{{ .Name }}, BuildHandles{{ .Name }});
{{- else }}
  return cpp_benchmarks::BuilderBenchmark(state, BuildFromEmptyContext{{ .Name }}, BuildEmptyContext{{ .Name }});
{{- end }}
}

bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	return cpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }});
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	return cpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }});
}
{{ end }}

void RegisterTests() {
	{{ range .Benchmarks }}
	perftest::RegisterTest("CPP/Builder/{{ .Path }}/Steps", BenchmarkBuilder{{ .Name }});
	perftest::RegisterTest("CPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
	perftest::RegisterTest("CPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
	{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmark struct {
	Path, Name, Type     string
	ValueBuild, ValueVar string
	HandleDefs           string
}

type benchmarkTmplInput struct {
	FidlLibrary string
	FidlInclude string
	Benchmarks  []benchmark
}

// Generate generates C++ natural type benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	tmplInput := benchmarkTmplInput{
		FidlLibrary: libraryName(config.CppBenchmarksFidlLibrary),
		FidlInclude: libraryInclude(config.CppBenchmarksFidlLibrary),
	}
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuild, valVar := buildValue(gidlBenchmark.Value, decl, handleReprRaw)
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, benchmark{
			Path:       gidlBenchmark.Name,
			Name:       benchmarkName(gidlBenchmark.Name),
			Type:       benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value),
			ValueBuild: valBuild,
			ValueVar:   valVar,
			HandleDefs: libhlcpp.BuildHandleDefs(gidlBenchmark.HandleDefs),
		})
	}
	var buf bytes.Buffer
	if err := benchmarkTmpl.Execute(&buf, tmplInput); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func libraryInclude(librarySuffix string) string {
	return fmt.Sprintf("fidl/test.benchmarkfidl%s/cpp/natural_types.h", strings.ReplaceAll(librarySuffix, " ", ""))
}
func libraryName(librarySuffix string) string {
	return fmt.Sprintf("test_benchmarkfidl%s", strings.ReplaceAll(librarySuffix, " ", ""))
}

func benchmarkTypeFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkProtocolFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
