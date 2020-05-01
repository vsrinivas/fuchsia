// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"fmt"
	"io"
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
{{- range .EncodeBenchmarks }}
	("Encode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_encode),
{{- end }}
{{- range .DecodeBenchmarks }}
	("Decode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_decode),
{{- end }}
];

const V1_CONTEXT: &Context = &Context {};

{{ range .EncodeBenchmarks }}
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
{{ end }}

{{ range .DecodeBenchmarks }}
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
	NumBenchmarks    int
	EncodeBenchmarks []encodeBenchmark
	DecodeBenchmarks []decodeBenchmark
}

type encodeBenchmark struct {
	Name, ChromeperfPath, Value string
}

type decodeBenchmark struct {
	Name, ChromeperfPath, Value, ValueType string
}

// GenerateBenchmarks generates Rust benchmarks.
func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	schema := gidlmixer.BuildSchema(fidl)
	encodeBenchmarks, err := encodeBenchmarks(gidl.EncodeBenchmark, schema)
	if err != nil {
		return err
	}
	decodeBenchmarks, err := decodeBenchmarks(gidl.DecodeBenchmark, schema)
	if err != nil {
		return err
	}
	input := benchmarkTmplInput{
		NumBenchmarks:    len(encodeBenchmarks) + len(decodeBenchmarks),
		EncodeBenchmarks: encodeBenchmarks,
		DecodeBenchmarks: decodeBenchmarks,
	}
	return benchmarkTmpl.Execute(wr, input)
}

func encodeBenchmarks(gidlEncodeBenchmarks []gidlir.EncodeBenchmark, schema gidlmixer.Schema) ([]encodeBenchmark, error) {
	var encodeBenchmarks []encodeBenchmark
	for _, gidlEncodeBenchmark := range gidlEncodeBenchmarks {
		decl, err := schema.ExtractDeclaration(gidlEncodeBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("encode benchmark %s: %s", gidlEncodeBenchmark.Name, err)
		}
		value := visit(gidlEncodeBenchmark.Value, decl)
		encodeBenchmarks = append(encodeBenchmarks, encodeBenchmark{
			Name:           benchmarkName(gidlEncodeBenchmark.Name),
			ChromeperfPath: gidlEncodeBenchmark.Name,
			Value:          value,
		})
	}
	return encodeBenchmarks, nil
}

func decodeBenchmarks(gidlDecodeBenchmarks []gidlir.DecodeBenchmark, schema gidlmixer.Schema) ([]decodeBenchmark, error) {
	var decodeBenchmarks []decodeBenchmark
	for _, gidlDecodeBenchmark := range gidlDecodeBenchmarks {
		decl, err := schema.ExtractDeclaration(gidlDecodeBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("decode benchmark %s: %s", gidlDecodeBenchmark.Name, err)
		}
		value := visit(gidlDecodeBenchmark.Value, decl)
		decodeBenchmarks = append(decodeBenchmarks, decodeBenchmark{
			Name:           benchmarkName(gidlDecodeBenchmark.Name),
			ChromeperfPath: gidlDecodeBenchmark.Name,
			Value:          value,
			ValueType:      declName(decl),
		})
	}
	return decodeBenchmarks, nil
}

func benchmarkName(gidlName string) string {
	return fidlcommon.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
