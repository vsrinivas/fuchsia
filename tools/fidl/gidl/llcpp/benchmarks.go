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

{{ range .BuilderBenchmarks }}
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
	using AllocatorType = fidl::BufferAllocator<MessageSize<{{ .Type }}>>;
	return llcpp_benchmarks::BuilderBenchmark<AllocatorType>(state, Build{{ .Name }}Allocator);
}
bool BenchmarkBuilder{{ .Name }}Unowned(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Unowned);
}
{{ end }}

{{ range .EncodeBenchmarks }}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::EncodeBenchmark(state, Build{{ .Name }}Heap);
}

bool BenchmarkMemcpy{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::MemcpyBenchmark(state, Build{{ .Name }}Heap);
}
{{ end }}

{{ range .DecodeBenchmarks }}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	return llcpp_benchmarks::DecodeBenchmark(state, Build{{ .Name }}Heap);
}
{{ end }}

void RegisterTests() {
	{{ range .BuilderBenchmarks }}
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Heap/WallTime",
						   BenchmarkBuilder{{ .Name }}Heap);
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Allocator/WallTime",
						   BenchmarkBuilder{{ .Name }}Allocator);
	perftest::RegisterTest("LLCPP/Builder/{{ .Path }}/Unowned/WallTime",
						   BenchmarkBuilder{{ .Name }}Unowned);
	{{ end }}

	{{ range .EncodeBenchmarks }}
	perftest::RegisterTest("LLCPP/Encode/{{ .Path }}/Steps", BenchmarkEncode{{ .Name }});
	perftest::RegisterTest("Memcpy/{{ .Path }}", BenchmarkMemcpy{{ .Name }});
	{{ end }}

	{{ range .DecodeBenchmarks }}
	perftest::RegisterTest("LLCPP/Decode/{{ .Path }}/Steps", BenchmarkDecode{{ .Name }});
	{{ end }}
}
PERFTEST_CTOR(RegisterTests)

} // namespace
`))

type benchmarksTmplInput struct {
	BuilderBenchmarks []builderBenchmark
	EncodeBenchmarks  []encodeBenchmark
	DecodeBenchmarks  []decodeBenchmark
}

type builderBenchmark struct {
	Path, Name, Type                       string
	ValueBuildHeap, ValueVarHeap           string
	ValueBuildAllocator, ValueVarAllocator string
	ValueBuildUnowned, ValueVarUnowned     string
}

type encodeBenchmark struct {
	Path, Name, Type string
}

type decodeBenchmark struct {
	Path, Name, Type string
}

// Generate generates Low-Level C++ benchmarks.
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
		valBuildUnowned, valVarUnowned := libllcpp.BuildValueUnowned(gidlEncodeBenchmark.Value, decl)
		valBuildHeap, valVarHeap := libllcpp.BuildValueHeap(gidlEncodeBenchmark.Value, decl)
		valBuildAllocator, valVarAllocator := libllcpp.BuildValueAllocator("allocator", gidlEncodeBenchmark.Value, decl)
		builderBenchmarks = append(builderBenchmarks, builderBenchmark{
			Path:                gidlEncodeBenchmark.Name,
			Name:                benchmarkName(gidlEncodeBenchmark.Name),
			Type:                benchmarkTypeFromValue(gidlEncodeBenchmark.Value),
			ValueBuildUnowned:   valBuildUnowned,
			ValueVarUnowned:     valVarUnowned,
			ValueBuildHeap:      valBuildHeap,
			ValueVarHeap:        valVarHeap,
			ValueBuildAllocator: valBuildAllocator,
			ValueVarAllocator:   valVarAllocator,
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
			Type: benchmarkTypeFromValue(gidlEncodeBenchmark.Value),
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
			Type: benchmarkTypeFromValue(gidlDecodeBenchmark.Value),
		})
	}
	return decodeBenchmarks, nil
}

func benchmarkTypeFromValue(value gidlir.Value) string {
	return fmt.Sprintf("llcpp::benchmarkfidl::%s", gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
