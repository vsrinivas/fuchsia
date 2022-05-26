// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	_ "embed"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var (
	//go:embed benchmarks.tmpl
	benchmarkTmplText string

	benchmarkTmpl = template.Must(template.New("benchmarkTmpl").Parse(benchmarkTmplText))
)

type benchmarkTmplInput struct {
	NumBenchmarks int
	CrateSuffix   string
	Benchmarks    []benchmark
}
type benchmark struct {
	Name, ChromeperfPath, HandleDefs, Value, ValueType string
	EnableSendEventBenchmark, EnableEchoCallBenchmark  bool
}

// GenerateBenchmarks generates Rust benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	nBenchmarks := 0
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Name:                     benchmarkName(gidlBenchmark.Name),
			ChromeperfPath:           gidlBenchmark.Name,
			HandleDefs:               buildHandleDefs(gidlBenchmark.HandleDefs),
			Value:                    value,
			ValueType:                declName(decl),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
		nBenchmarks += 3
		if gidlBenchmark.EnableSendEventBenchmark {
			nBenchmarks++
		}
		if gidlBenchmark.EnableEchoCallBenchmark {
			nBenchmarks++
		}
	}
	input := benchmarkTmplInput{
		NumBenchmarks: nBenchmarks,
		CrateSuffix:   config.RustBenchmarksFidlLibrary,
		Benchmarks:    benchmarks,
	}
	var buf bytes.Buffer
	err := benchmarkTmpl.Execute(&buf, input)
	return buf.Bytes(), err
}

func benchmarkName(gidlName string) string {
	return fidlgen.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
