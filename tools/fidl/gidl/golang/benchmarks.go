// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"io"
	"strings"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
package benchmark_suite

import (
	"testing"

	"fidl/benchmarkfidl"

	"syscall/zx/fidl"
)

{{ range .EncodeBenchmarks }}
func BenchmarkEncode{{ .Name }}(b *testing.B) {
	data := make([]byte, 65536)
	input := {{ .Value }}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _, err := fidl.Marshal(&input, data, nil)
		if err != nil {
			b.Fatal(err)
		}
	}
}
{{ end }}

{{ range .DecodeBenchmarks }}
func BenchmarkDecode{{ .Name }}(b *testing.B) {
	data := make([]byte, 65536)
	input := {{ .Value }}
	_, _, err := fidl.Marshal(&input, data, nil)
	if err != nil {
		b.Fatal(err)
	}

	var output {{ .ValueType }}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _, err := fidl.Unmarshal(data, nil, &output)
		if err != nil {
			b.Fatal(err)
		}
	}
}
{{ end }}

type Benchmark struct {
	Label string
	BenchFunc func(*testing.B)
}

// Benchmarks is read by go_fidl_benchmarks_lib.
var Benchmarks = []Benchmark{
{{ range .EncodeBenchmarks }}
	{
		Label: "Encode/{{ .ChromeperfPath }}",
		BenchFunc: BenchmarkEncode{{ .Name }},
	},
{{ end }}
{{ range .DecodeBenchmarks }}
	{
		Label: "Decode/{{ .ChromeperfPath }}",
		BenchFunc: BenchmarkDecode{{ .Name }},
	},
{{ end }}
}
`))

type benchmarkTmplInput struct {
	EncodeBenchmarks []goEncodeBenchmark
	DecodeBenchmarks []goDecodeBenchmark
}

type goEncodeBenchmark struct {
	Name, ChromeperfPath, Value string
}

type goDecodeBenchmark struct {
	Name, ChromeperfPath, Value, ValueType string
}

// GenerateBenchmarks generates Go benchmarks.
func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	schema := gidlmixer.BuildSchema(fidl)
	encodeBenchmarks, err := goEncodeBenchmarks(gidl.EncodeBenchmark, schema)
	if err != nil {
		return err
	}
	decodeBenchmarks, err := goDecodeBenchmarks(gidl.DecodeBenchmark, schema)
	if err != nil {
		return err
	}
	input := benchmarkTmplInput{
		EncodeBenchmarks: encodeBenchmarks,
		DecodeBenchmarks: decodeBenchmarks,
	}
	return benchmarkTmpl.Execute(wr, input)
}

func goEncodeBenchmarks(gidlEncodeBenchmarks []gidlir.EncodeBenchmark, schema gidlmixer.Schema) ([]goEncodeBenchmark, error) {
	var goEncodeBenchmarks []goEncodeBenchmark
	for _, encodeBenchmark := range gidlEncodeBenchmarks {
		decl, err := schema.ExtractDeclaration(encodeBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("encode benchmark %s: %s", encodeBenchmark.Name, err)
		}
		value := visit(encodeBenchmark.Value, decl)
		goEncodeBenchmarks = append(goEncodeBenchmarks, goEncodeBenchmark{
			Name:           goBenchmarkName(encodeBenchmark.Name),
			ChromeperfPath: encodeBenchmark.Name,
			Value:          value,
		})
	}
	return goEncodeBenchmarks, nil
}

func goDecodeBenchmarks(gidlDecodeBenchmarks []gidlir.DecodeBenchmark, schema gidlmixer.Schema) ([]goDecodeBenchmark, error) {
	var goDecodeBenchmarks []goDecodeBenchmark
	for _, decodeBenchmark := range gidlDecodeBenchmarks {
		decl, err := schema.ExtractDeclaration(decodeBenchmark.Value)
		if err != nil {
			return nil, fmt.Errorf("decode benchmark %s: %s", decodeBenchmark.Name, err)
		}
		value := visit(decodeBenchmark.Value, decl)
		goDecodeBenchmarks = append(goDecodeBenchmarks, goDecodeBenchmark{
			Name:           goBenchmarkName(decodeBenchmark.Name),
			ChromeperfPath: decodeBenchmark.Name,
			Value:          value,
			ValueType:      declName(decl),
		})
	}
	return goDecodeBenchmarks, nil
}

func goBenchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
