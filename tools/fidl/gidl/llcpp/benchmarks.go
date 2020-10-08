// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/cpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	libllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/llcpp/lib"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("tmpl").Parse(`
#include <{{ .FidlLibrary }}/llcpp/fidl.h>
#include <lib/fidl/cpp/test/handle_util.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/llcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/encode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/echo_call_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/send_event_benchmark_util.h"

namespace {

{{ range .Benchmarks }}
{{- if .HandleDefs }}
std::vector<zx_handle_t> BuildHandles{{ .Name }}() {
	return {{ .HandleDefs }};
}

{{ .Type }} BuildFromHandles{{ .Name }}(const std::vector<zx_handle_t>& handle_defs) {
  {{ .ValueBuildHeap }}
  auto result =  {{ .ValueVarHeap }};
  return result;
}

{{ .Type }} Build{{ .Name }}Heap() {
  return BuildFromHandles{{ .Name }}(BuildHandles{{ .Name }}());
}
{{- else }}
std::tuple<> BuildEmptyContext{{ .Name }}() {
	return std::make_tuple();
}

{{ .Type }} BuildFromEmptyContext{{ .Name }}(std::tuple<> _context) {
	{{ .ValueBuildHeap }}
	auto result = {{ .ValueVarHeap }};
	return result;
}

{{ .Type }} Build{{ .Name }}Heap() {
  {{ .ValueBuildHeap }}
  auto result = {{ .ValueVarHeap }};
  return result;
}
{{- end }}

bool BenchmarkBuilder{{ .Name }}Heap(perftest::RepeatState* state) {
{{- if .HandleDefs }}
  return llcpp_benchmarks::BuilderBenchmark(state, BuildFromHandles{{ .Name }}, BuildHandles{{ .Name }});
{{- else }}
  return llcpp_benchmarks::BuilderBenchmark(state, BuildFromEmptyContext{{ .Name }}, BuildEmptyContext{{ .Name }});
{{- end }}
}

bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }}Heap);
}
{{ if .EnableSendEventBenchmark }}
bool BenchmarkSendEvent{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::SendEventBenchmark<{{ .EventProtocolType }}>(state, Build{{ .Name }}Heap);
}
{{- end -}}
{{ if .EnableEchoCallBenchmark }}
bool BenchmarkEchoCall{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::EchoCallBenchmark<{{ .EchoCallProtocolType }}>(state, Build{{ .Name }}Heap);
}
{{- end -}}
{{ end }}

void RegisterTests() {
	{{ range .Benchmarks }}
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Steps", BenchmarkBuilder{{ .Name }}Heap);
	perftest::RegisterTest("LLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
	perftest::RegisterTest("LLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
	{{ if .EnableSendEventBenchmark }}
	perftest::RegisterTest("LLCPP/SendEvent/{{ .Path }}/Steps", BenchmarkSendEvent{{ .Name }});
	{{- end -}}
	{{ if .EnableEchoCallBenchmark }}
	perftest::RegisterTest("LLCPP/EchoCall/{{ .Path }}/Steps", BenchmarkEchoCall{{ .Name }});
	{{- end -}}
	{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmark struct {
	Path, Name, Type, EventProtocolType, EchoCallProtocolType string
	ValueBuildHeap, ValueVarHeap                              string
	HandleDefs                                                string
	EnableSendEventBenchmark, EnableEchoCallBenchmark         bool
}

type benchmarkTmplInput struct {
	FidlLibrary string
	Benchmarks  []benchmark
}

// Generate generates Low-Level C++ benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	tmplInput := benchmarkTmplInput{
		FidlLibrary: libraryName(config.CppBenchmarksFidlLibrary),
	}
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuildHeap, valVarHeap := libllcpp.BuildValueHeap(gidlBenchmark.Value, decl)
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, benchmark{
			Path:                     gidlBenchmark.Name,
			Name:                     benchmarkName(gidlBenchmark.Name),
			Type:                     benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value),
			EventProtocolType:        benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EventProtocol",
			EchoCallProtocolType:     benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EchoCall",
			ValueBuildHeap:           valBuildHeap,
			ValueVarHeap:             valVarHeap,
			HandleDefs:               libcpp.BuildHandleDefs(gidlBenchmark.HandleDefs),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
	}
	var buf bytes.Buffer
	if err := benchmarkTmpl.Execute(&buf, tmplInput); err != nil {
		return nil, nil, err
	}
	return buf.Bytes(), nil, nil
}

func libraryName(librarySuffix string) string {
	return fmt.Sprintf("benchmarkfidl%s", strings.ReplaceAll(librarySuffix, " ", ""))
}

func benchmarkTypeFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("llcpp::%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
