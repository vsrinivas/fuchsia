// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package driver_cpp

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	libllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/llcpp/lib"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var benchmarkTmpl = template.Must(template.New("tmpl").Parse(`
#include <{{ .FidlInclude }}>
#include <cts/tests/pkg/fidl/cpp/test/handle_util.h>
#include <perftest/perftest.h>

#include <vector>

#include "src/tests/benchmarks/fidl/driver_cpp/echo_call_benchmark_util.h"
#include "src/tests/benchmarks/fidl/driver_cpp/echo_call_async_benchmark_util.h"

namespace {

{{ range .Benchmarks }}
{{- if .HandleDefs }}
std::vector<zx_handle_t> BuildHandles{{ .Name }}() {
	return {{ .HandleDefs }};
}

{{ .Type }} BuildFromHandles{{ .Name }}(fidl::AnyArena& allocator, const std::vector<zx_handle_t>& handle_defs) {
  {{ .ValueBuild }}
  auto result =  {{ .ValueVar }};
  return result;
}

{{ .Type }} Build{{ .Name }}(fidl::AnyArena& allocator) {
  return BuildFromHandles{{ .Name }}(allocator, BuildHandles{{ .Name }}());
}
{{- else }}

{{ .Type }} Build{{ .Name }}(fidl::AnyArena& allocator) {
  {{ .ValueBuild }}
  auto result = {{ .ValueVar }};
  return result;
}
{{- end }}

bool BenchmarkEchoCall{{ .Name }}(perftest::RepeatState* state) {
	return driver_benchmarks::EchoCallBenchmark<{{ .EchoCallProtocolType }}>(state, Build{{ .Name }});
}

bool BenchmarkEchoCallAsync{{ .Name }}(perftest::RepeatState* state) {
	return driver_benchmarks::EchoCallAsyncBenchmark<{{ .EchoCallProtocolType }}>(state, Build{{ .Name }});
}
{{- end -}}

void RegisterTests() {
	{{ range .Benchmarks }}
	perftest::RegisterTest("DriverCPP/EchoCall/{{ .Path }}/Steps", BenchmarkEchoCall{{ .Name }});
	perftest::RegisterTest("DriverCPP/EchoCallAsync/{{ .Path }}/Steps", BenchmarkEchoCallAsync{{ .Name }});
	{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmark struct {
	Path, Name, Type, EchoCallProtocolType string
	ValueBuild, ValueVar                   string
	HandleDefs                             string
}

type benchmarkTmplInput struct {
	FidlLibrary string
	FidlInclude string
	Benchmarks  []benchmark
}

// Generate generates Low-Level C++ benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	tmplInput := benchmarkTmplInput{
		FidlLibrary: libraryName(config.CppBenchmarksFidlLibrary),
		FidlInclude: libraryInclude(config.CppBenchmarksFidlLibrary),
	}
	for _, gidlBenchmark := range gidl.Benchmark {
		if !gidlBenchmark.EnableEchoCallBenchmark {
			continue
		}
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuild, valVar := libllcpp.BuildValueAllocator("allocator", gidlBenchmark.Value, decl, libllcpp.HandleReprRaw)
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, benchmark{
			Path:                 gidlBenchmark.Name,
			Name:                 benchmarkName(gidlBenchmark.Name),
			Type:                 benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value),
			EchoCallProtocolType: benchmarkProtocolFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EchoCallDriver",
			ValueBuild:           valBuild,
			ValueVar:             valVar,
			HandleDefs:           libhlcpp.BuildHandleDefs(gidlBenchmark.HandleDefs),
		})
	}
	var buf bytes.Buffer
	if err := benchmarkTmpl.Execute(&buf, tmplInput); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func libraryInclude(librarySuffix string) string {
	return fmt.Sprintf("fidl/test.benchmarkfidl%s/cpp/driver/wire.h", strings.ReplaceAll(librarySuffix, " ", ""))
}
func libraryName(librarySuffix string) string {
	return fmt.Sprintf("test_benchmarkfidl%s", strings.ReplaceAll(librarySuffix, " ", ""))
}

func benchmarkTypeFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::wire::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkProtocolFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
