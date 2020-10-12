// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("tmpl").Parse(`
#include <{{ .FidlLibrary }}/cpp/fidl.h>
#include <lib/fidl/cpp/test/handle_util.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/hlcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/encode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/echo_call_benchmark_util.h"
#include "src/tests/benchmarks/fidl/hlcpp/send_event_benchmark_util.h"

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
  return hlcpp_benchmarks::BuilderBenchmark(state, BuildFromHandles{{ .Name }}, BuildHandles{{ .Name }});
{{- else }}
  return hlcpp_benchmarks::BuilderBenchmark(state, BuildFromEmptyContext{{ .Name }}, BuildEmptyContext{{ .Name }});
{{- end }}
}

bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }});
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
  return hlcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }});
}
{{ if .EnableSendEventBenchmark }}
bool BenchmarkSendEvent{{ .Name }}(perftest::RepeatState* state) {
	return hlcpp_benchmarks::SendEventBenchmark<{{ .EventProtocolType }}>(state, Build{{ .Name }});
}
{{- end -}}
{{ if .EnableEchoCallBenchmark }}
bool BenchmarkEchoCall{{ .Name }}(perftest::RepeatState* state) {
	return hlcpp_benchmarks::EchoCallBenchmark<{{ .EchoCallProtocolType }}>(state, Build{{ .Name }});
}
{{- end -}}
{{ end }}

void RegisterTests() {
  {{ range .Benchmarks }}
  perftest::RegisterTest("HLCPP/Builder/{{ .Path }}/Steps", BenchmarkBuilder{{ .Name }});
  perftest::RegisterTest("HLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
  perftest::RegisterTest("HLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
  {{ if .EnableSendEventBenchmark }}
  perftest::RegisterTest("HLCPP/SendEvent/{{ .Path }}/Steps", BenchmarkSendEvent{{ .Name }});
  {{- end -}}
  {{ if .EnableEchoCallBenchmark }}
  perftest::RegisterTest("HLCPP/EchoCall/{{ .Path }}/Steps", BenchmarkEchoCall{{ .Name }});
  {{- end -}}
  {{ end }}
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
`))

type benchmark struct {
	Path, Name, Type                                  string
	ValueBuild, ValueVar                              string
	HandleDefs                                        string
	EventProtocolType, EchoCallProtocolType           string
	EnableSendEventBenchmark, EnableEchoCallBenchmark bool
}

type benchmarkTmplInput struct {
	FidlLibrary string
	Benchmarks  []benchmark
}

// Generate generates High-Level C++ benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	tmplInput := benchmarkTmplInput{
		FidlLibrary: libraryName(config.CppBenchmarksFidlLibrary),
	}
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valueBuilder := newCppValueBuilder()
		valueVar := valueBuilder.visit(gidlBenchmark.Value, decl)
		valueBuild := valueBuilder.String()
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, benchmark{
			Path:                     gidlBenchmark.Name,
			Name:                     benchmarkName(gidlBenchmark.Name),
			Type:                     benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value),
			ValueBuild:               valueBuild,
			ValueVar:                 valueVar,
			HandleDefs:               BuildHandleDefs(gidlBenchmark.HandleDefs),
			EventProtocolType:        benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EventProtocol",
			EchoCallProtocolType:     benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EchoCall",
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
	}
	var buf bytes.Buffer
	if err := benchmarkTmpl.Execute(&buf, tmplInput); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func libraryName(librarySuffix string) string {
	return fmt.Sprintf("benchmarkfidl%s", strings.ReplaceAll(librarySuffix, " ", ""))
}

func benchmarkTypeFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}

func BuildHandleDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("std::vector<zx_handle_t>{\n")
	for i, d := range defs {
		switch d.Subtype {
		case fidlir.Channel:
			builder.WriteString("fidl::test::util::create_channel(),")
		case fidlir.Event:
			builder.WriteString("fidl::test::util::create_event(),")
		default:
			panic(fmt.Sprintf("unsupported handle subtype: %s", d.Subtype))
		}
		// Write indices corresponding to the .gidl file handle_defs block.
		builder.WriteString(fmt.Sprintf(" // #%d\n", i))
	}
	builder.WriteString("}")
	return builder.String()
}
