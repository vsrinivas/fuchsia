// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llcpp

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

#include "src/tests/benchmarks/fidl/llcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/encode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/memcpy_benchmark_util.h"

namespace {

{{ range .Benchmarks }}
{{ .Type }} Build{{ .Name }}Heap() {
	{{ .ValueBuildHeap }}
	auto obj = {{ .ValueVarHeap }};
	return obj;
}
{{ .Type }} Build{{ .Name }}Allocator(fidl::Allocator* allocator) {
	{{ .ValueBuildAllocator }}
	auto obj = {{ .ValueVarAllocator }};
	return obj;
}
{{ .Type }} Build{{ .Name }}Unowned() {
	{{ .ValueBuildUnowned }}
	auto obj = std::move({{ .ValueVarUnowned }});
	return obj;
}
bool BenchmarkBuilder{{ .Name }}Heap(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkBuilder{{ .Name }}Allocator(perftest::RepeatState* state) {
	using AllocatorType = fidl::UnsafeBufferAllocator<
		fidl::internal::ClampedMessageSize<{{ .Type }}, fidl::MessageDirection::kSending>()>;
	return llcpp_benchmarks::BuilderBenchmark<AllocatorType>(state, Build{{ .Name }}Allocator);
}
bool BenchmarkBuilder{{ .Name }}Unowned(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Unowned);
}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkMemcpy{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::MemcpyBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }}Heap);
}
{{ end }}

void RegisterTests() {
	{{ range .Benchmarks }}
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Heap/WallTime",
						   BenchmarkBuilder{{ .Name }}Heap);
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Allocator/WallTime",
						   BenchmarkBuilder{{ .Name }}Allocator);
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Unowned/WallTime",
						   BenchmarkBuilder{{ .Name }}Unowned);
	perftest::RegisterTest("LLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
	perftest::RegisterTest("LLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
	perftest::RegisterTest("Memcpy/{{ .Path }}", BenchmarkMemcpy{{ .Name }});
	{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmarksTmplInput struct {
	Benchmarks []benchmark
}

type benchmark struct {
	Path, Name, Type                       string
	ValueBuildHeap, ValueVarHeap           string
	ValueBuildAllocator, ValueVarAllocator string
	ValueBuildUnowned, ValueVarUnowned     string
}

// Generate generates Low-Level C++ benchmarks.
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
		valBuildUnowned, valVarUnowned := libllcpp.BuildValueUnowned(gidlBenchmark.Value, decl)
		valBuildHeap, valVarHeap := libllcpp.BuildValueHeap(gidlBenchmark.Value, decl)
		valBuildAllocator, valVarAllocator := libllcpp.BuildValueAllocator("allocator", gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Path:                gidlBenchmark.Name,
			Name:                benchmarkName(gidlBenchmark.Name),
			Type:                benchmarkTypeFromValue(gidlBenchmark.Value),
			ValueBuildUnowned:   valBuildUnowned,
			ValueVarUnowned:     valVarUnowned,
			ValueBuildHeap:      valBuildHeap,
			ValueVarHeap:        valVarHeap,
			ValueBuildAllocator: valBuildAllocator,
			ValueVarAllocator:   valVarAllocator,
		})
	}
	return benchmarksTmpl.Execute(wr, benchmarksTmplInput{
		Benchmarks: benchmarks,
	})
}

func benchmarkTypeFromValue(value gidlir.Value) string {
	return fmt.Sprintf("llcpp::benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
