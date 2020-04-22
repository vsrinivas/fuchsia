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
	gidlmixer "gidl/mixer"
)

var benchmarksTmpl = template.Must(template.New("tmpl").Parse(`
#include <benchmarkfidl/llcpp/fidl.h>
#include <perftest/perftest.h>

#include "src/tests/benchmarks/fidl/llcpp/builder_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/decode_benchmark_util.h"
#include "src/tests/benchmarks/fidl/llcpp/encode_benchmark_util.h"

constexpr size_t allocator_buf_size = 65536;

namespace {

{{ range .BuilderBenchmarks }}
{{ .Type }} Build{{ .Name }}Heap(perftest::RepeatState* state) {
	{{ .ValueBuildHeap }}
	auto obj = {{ .ValueVarHeap }};
	if (state != nullptr)
	  state->NextStep();  // Next: Destructors
	return obj;
}
{{ .Type }} Build{{ .Name }}Allocator(perftest::RepeatState* state, fidl::Allocator* allocator) {
	{{ .ValueBuildAllocator }}
	auto obj = {{ .ValueVarAllocator }};
	if (state != nullptr)
	  state->NextStep();  // Next: Destructors
	return obj;
}
{{ .Type }} Build{{ .Name }}Unowned(perftest::RepeatState* state) {
	{{ .ValueBuildUnowned }}
	auto obj = std::move({{ .ValueVarUnowned }});
	if (state != nullptr)
	  state->NextStep();  // Next: Destructors
	return obj;
}
bool BenchmarkBuilder{{ .Name }}Heap(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Heap);
}
bool BenchmarkBuilder{{ .Name }}Allocator(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark<fidl::BufferAllocator<allocator_buf_size>>(state, Build{{ .Name }}Allocator);
}
bool BenchmarkBuilder{{ .Name }}Unowned(perftest::RepeatState* state) {
	return llcpp_benchmarks::BuilderBenchmark(state, Build{{ .Name }}Unowned);
}
{{ end }}

{{ range .EncodeBenchmarks }}
bool BenchmarkEncode{{ .Name }}(perftest::RepeatState* state) {
	fidl::aligned<{{ .Type }}> obj = Build{{ .Name }}Heap(nullptr);
	return llcpp_benchmarks::EncodeBenchmark(state, &obj);
}
{{ end }}

{{ range .DecodeBenchmarks }}
bool BenchmarkDecode{{ .Name }}(perftest::RepeatState* state) {
	fidl::aligned<{{ .Type }}>  obj = Build{{ .Name }}Heap(nullptr);
	return llcpp_benchmarks::DecodeBenchmark(state, &obj);
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
		valBuildUnowned, valVarUnowned := buildValueUnowned(gidlEncodeBenchmark.Value, decl)
		valBuildHeap, valVarHeap := buildValueHeap(gidlEncodeBenchmark.Value, decl)
		valBuildAllocator, valVarAllocator := buildValueAllocator("allocator", gidlEncodeBenchmark.Value, decl)
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
