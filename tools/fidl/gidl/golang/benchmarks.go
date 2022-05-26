// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

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
	Benchmarks []benchmark
}

type benchmark struct {
	Name, ChromeperfPath, Value, ValueType            string
	EnableSendEventBenchmark, EnableEchoCallBenchmark bool
}

// GenerateBenchmarks generates Go benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Name:                     goBenchmarkName(gidlBenchmark.Name),
			ChromeperfPath:           gidlBenchmark.Name,
			Value:                    value,
			ValueType:                declName(decl),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
	}
	input := benchmarkTmplInput{
		Benchmarks: benchmarks,
	}
	var buf bytes.Buffer
	err := withGoFmt{benchmarkTmpl}.Execute(&buf, input)
	return buf.Bytes(), err
}

func goBenchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
