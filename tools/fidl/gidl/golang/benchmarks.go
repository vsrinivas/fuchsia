// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"io"
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
	{{ .ValueBuild }}
	input := {{ .ValueVar }}
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
	data := {{ .Bytes }}
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
		Label: "Encode/{{ .Name }}",
		BenchFunc: BenchmarkEncode{{ .Name }},
	},
{{ end }}
{{ range .DecodeBenchmarks }}
	{
		Label: "Decode/{{ .Name }}",
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
	Name, ValueBuild, ValueVar string
}

type goDecodeBenchmark struct {
	Name, ValueType, Bytes string
}

// GenerateBenchmarks generates Go benchmarks.
func GenerateBenchmarks(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	encodeBenchmarks, err := goEncodeBenchmarks(gidl.EncodeBenchmark, fidl)
	if err != nil {
		return err
	}
	decodeBenchmarks, err := goDecodeBenchmarks(gidl.DecodeBenchmark, fidl)
	if err != nil {
		return err
	}
	input := benchmarkTmplInput{
		EncodeBenchmarks: encodeBenchmarks,
		DecodeBenchmarks: decodeBenchmarks,
	}
	return benchmarkTmpl.Execute(wr, input)
}

func goEncodeBenchmarks(gidlEncodeBenchmarks []gidlir.EncodeBenchmark, fidl fidlir.Root) ([]goEncodeBenchmark, error) {
	var goEncodeBenchmarks []goEncodeBenchmark
	for _, encodeBenchmark := range gidlEncodeBenchmarks {
		decl, err := gidlmixer.ExtractDeclaration(encodeBenchmark.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeBenchmark %s: %s", encodeBenchmark.Name, err)
		}
		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeBenchmark.Value, decl)
		valueBuild := valueBuilder.String()
		valueVar := valueBuilder.lastVar
		goEncodeBenchmarks = append(goEncodeBenchmarks, goEncodeBenchmark{
			Name:       encodeBenchmark.Name,
			ValueBuild: valueBuild,
			ValueVar:   valueVar,
		})
	}
	return goEncodeBenchmarks, nil
}

func goDecodeBenchmarks(gidlDecodeBenchmarks []gidlir.DecodeBenchmark, fidl fidlir.Root) ([]goDecodeBenchmark, error) {
	var goDecodeBenchmarks []goDecodeBenchmark
	for _, decodeBenchmark := range gidlDecodeBenchmarks {
		for _, encoding := range decodeBenchmark.Encodings {
			goDecodeBenchmarks = append(goDecodeBenchmarks, goDecodeBenchmark{
				Name:      decodeBenchmark.Name,
				Bytes:     bytesBuilder(encoding.Bytes),
				ValueType: "benchmarkfidl." + decodeBenchmark.Type,
			})
		}
	}
	return goDecodeBenchmarks, nil
}
