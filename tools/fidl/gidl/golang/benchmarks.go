// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
package benchmark_suite

import (
	"context"
	"sync"
	"testing"

	"fidl/benchmarkfidl"

	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)


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

{{ range .Benchmarks }}
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

{{ if .EnableEchoCallBenchmark }}
type {{ .Name }}EchoCallService struct {}

func (s *{{ .Name }}EchoCallService) Echo(ctx fidl.Context, val {{ .ValueType }}) ({{ .ValueType }}, error) {
	return val, nil
}

func BenchmarkEchoCall{{ .Name }}(b *testing.B) {
	client_end, server_end, err := zx.NewChannel(0)
	if err != nil {
		b.Fatal(err)
	}
	defer client_end.Close()
	stub := &{{ .ValueType }}EchoCallWithCtxStub{
		Impl: &{{ .Name }}EchoCallService{},
	}
	go component.ServeExclusive(context.Background(), stub, server_end, func (err error) {
		b.Fatal(err)
	})
	proxy := {{ .ValueType }}EchoCallWithCtxInterface(fidl.ChannelProxy{Channel: client_end})

	input := {{ .Value }}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := proxy.Echo(context.Background(), input); err != nil {
			b.Fatal(err)
		}
	}
}
{{- end -}}
{{ end }}

type Benchmark struct {
	Label string
	BenchFunc func(*testing.B)
}

// Benchmarks is read by go_fidl_benchmarks_lib.
var Benchmarks = []Benchmark{
{{ range .Benchmarks }}
	{
		Label: "Encode/{{ .ChromeperfPath }}",
		BenchFunc: BenchmarkEncode{{ .Name }},
	},
	{
		Label: "Decode/{{ .ChromeperfPath }}",
		BenchFunc: BenchmarkDecode{{ .Name }},
	},
	{{ if .EnableEchoCallBenchmark }}
	{
		Label: "EchoCall/{{ .ChromeperfPath }}",
		BenchFunc: BenchmarkEchoCall{{ .Name }},
	},
	{{- end -}}
{{ end }}
}
`))

type benchmarkTmplInput struct {
	Benchmarks []benchmark
}

type benchmark struct {
	Name, ChromeperfPath, Value, ValueType string
	EnableEchoCallBenchmark                bool
}

// GenerateBenchmarks generates Go benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Name:                    goBenchmarkName(gidlBenchmark.Name),
			ChromeperfPath:          gidlBenchmark.Name,
			Value:                   value,
			ValueType:               declName(decl),
			EnableEchoCallBenchmark: gidlBenchmark.EnableEchoCallBenchmark,
		})
	}
	input := benchmarkTmplInput{
		Benchmarks: benchmarks,
	}
	var buf bytes.Buffer
	err := withGoFmt{benchmarkTmpl}.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func goBenchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
