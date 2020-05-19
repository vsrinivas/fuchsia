// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
use {
	fidl::{
		encoding::{Context, Decodable, Decoder, Encoder},
		handle::Handle,
	},
	fidl_benchmarkfidl as benchmarkfidl,
	fuchsia_criterion::criterion::{BatchSize, Bencher},
};

// ALL_BENCHMARKS is used by src/tests/benchmarks/fidl/rust/src/main.rs.
pub const ALL_BENCHMARKS: [(&'static str, fn(&mut Bencher)); {{ .NumBenchmarks }}] = [
{{- range .Benchmarks }}
	("Encode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_encode),
	("Decode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_decode),
{{- end }}
];

const V1_CONTEXT: &Context = &Context {};

{{ range .Benchmarks }}
fn benchmark_{{ .Name }}_encode(b: &mut Bencher) {
	let bytes = &mut Vec::<u8>::with_capacity(65536);
	{{- /* TODO(fxb/36441): Revisit this when adding support for handles. */}}
	let handles = &mut Vec::<Handle>::with_capacity(64);
	b.iter_batched_ref(
		|| {
			{{ .Value }}
		},
		|value| {
			{{- /* Note: Encoding will truncate bytes and handles first. */}}
			Encoder::encode_with_context(V1_CONTEXT, bytes, handles, value).unwrap();
		},
		BatchSize::SmallInput,
	);
}
fn benchmark_{{ .Name }}_decode(b: &mut Bencher) {
	let bytes = &mut Vec::<u8>::new();
	{{- /* TODO(fxb/36441): Revisit this when adding support for handles. */}}
	let handles = &mut Vec::<Handle>::new();
	let original_value = &mut {{ .Value }};
	Encoder::encode_with_context(V1_CONTEXT, bytes, handles, original_value).unwrap();

	b.iter_batched_ref(
		{{- /* We use a fresh empty value for each run rather than decoding into
			   the same value every time. The latter would be less realistic
			   since e.g. vectors would only allocate on the first run. */}}
		{{ .ValueType }}::new_empty,
		|value| {
			Decoder::decode_with_context(V1_CONTEXT, bytes, handles, value).unwrap();
		},
		BatchSize::SmallInput,
	);
}
{{ end }}
`))

type benchmarkTmplInput struct {
	NumBenchmarks int
	Benchmarks    []benchmark
}

type benchmark struct {
	Name, ChromeperfPath, Value, ValueType string
}

// GenerateBenchmarks generates Rust benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root) (map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Name:           benchmarkName(gidlBenchmark.Name),
			ChromeperfPath: gidlBenchmark.Name,
			Value:          value,
			ValueType:      declName(decl),
		})
	}
	input := benchmarkTmplInput{
		NumBenchmarks: len(benchmarks) * 2,
		Benchmarks:    benchmarks,
	}
	var buf bytes.Buffer
	err := benchmarkTmpl.Execute(&buf, input)
	return map[string][]byte{"": buf.Bytes()}, err
}

func benchmarkName(gidlName string) string {
	return fidlcommon.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
