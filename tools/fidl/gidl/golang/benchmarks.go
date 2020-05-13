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
	"sync"
	"testing"

	"fidl/benchmarkfidl"

	"syscall/zx"
	"syscall/zx/fidl")


type pools struct {
	bytes sync.Pool
	handleInfos sync.Pool
	handleDispositions sync.Pool
}

func newPools() *pools {
	return &pools{
		bytes: sync.Pool{
			New: func() interface{} {
				return make([]byte, zx.ChannelMaxMessageBytes)
			},
		},
		handleInfos: sync.Pool{
			New: func() interface{} {
				return make([]zx.HandleInfo, zx.ChannelMaxMessageHandles)
			},
		},
		handleDispositions: sync.Pool{
			New: func() interface{} {
				return make([]zx.HandleDisposition, zx.ChannelMaxMessageHandles)
			},
		},
	}
}

func (p *pools) useOnce() {
	p.bytes.Put(p.bytes.Get().([]byte))
	p.handleInfos.Put(p.handleInfos.Get().([]zx.HandleInfo))
	p.handleDispositions.Put(p.handleDispositions.Get().([]zx.HandleDisposition))
}

{{ range .EncodeBenchmarks }}
func BenchmarkEncode{{ .Name }}(b *testing.B) {
	pools := newPools()
	pools.useOnce()
	input := {{ .Value }}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		// This should be kept in sync with the buffer allocation strategy used in Go bindings.
		respb := pools.bytes.Get().([]byte)
		resphd := pools.handleDispositions.Get().([]zx.HandleDisposition)
		_, _, err := fidl.Marshal(&input, respb, resphd)
		if err != nil {
			b.Fatal(err)
		}
		pools.bytes.Put(respb)
		pools.handleDispositions.Put(resphd)
	}
}

func EncodeCount{{ .Name }}() (int, int, error) {
	bytes := make([]byte, 65536)
	handles := make([]zx.HandleDisposition, 64)
	input := {{ .Value }}
	return fidl.Marshal(&input, bytes, handles)
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

type EncodeCount struct {
	Label string
	Func func() (nbytes int, nhandles int, err error)
}

// EncodeCounts is read by go_fidl_benchmarks_lib.
var EncodeCounts = []EncodeCount{
	{{ range .EncodeBenchmarks }}
	{
		Label: "{{ .ChromeperfPath }}",
		Func: EncodeCount{{ .Name }},
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
	return withGoFmt{benchmarkTmpl}.Execute(wr, input)
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
