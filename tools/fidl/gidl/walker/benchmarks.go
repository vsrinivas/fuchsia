// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package walker

import (
	"fmt"
	"io"
	"strings"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	libllcpp "gidl/llcpp/lib"
	gidlmixer "gidl/mixer"
)

var benchmarksTmpl = template.Must(template.New("tmpl").Parse(`
#include <benchmarkfidl/llcpp/fidl.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/walker/walker_benchmark_util.h"

namespace {

{{ range .Benchmarks }}
{{ .Type }} Build{{ .Name }}() {
	{{ .ValueBuild }}
	auto obj = {{ .ValueVar }};
	return obj;
}

bool BenchmarkWalker{{ .Name }}(perftest::RepeatState* state) {
	return walker_benchmarks::WalkerBenchmark(state, Build{{ .Name }});
}
{{ end }}

void RegisterTests() {
{{ range .Benchmarks }}
	perftest::RegisterTest("Walker/{{ .Path }}/WallTime", BenchmarkWalker{{ .Name }});
{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmarksTmplInput struct {
	Benchmarks []walkerBenchmark
}

type walkerBenchmark struct {
	Path, Name, Type     string
	ValueBuild, ValueVar string
}

func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	schema := gidlmixer.BuildSchema(fidl)
	var tmplInput benchmarksTmplInput
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value)
		if err != nil {
			return fmt.Errorf("walker benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuild, valVar := libllcpp.BuildValueHeap(gidlBenchmark.Value, decl)
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, walkerBenchmark{
			Path:       gidlBenchmark.Name,
			Name:       benchmarkName(gidlBenchmark.Name),
			Type:       llcppBenchmarkType(gidlBenchmark.Value),
			ValueBuild: valBuild,
			ValueVar:   valVar,
		})
	}
	return benchmarksTmpl.Execute(wr, tmplInput)
}

func llcppBenchmarkType(value gidlir.Value) string {
	return fmt.Sprintf("llcpp::benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
