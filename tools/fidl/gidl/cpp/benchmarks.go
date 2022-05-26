// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"bytes"
	_ "embed"
	"fmt"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	libhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var (
	//go:embed benchmarks.tmpl
	benchmarkTmplText string

	benchmarkTmpl = template.Must(template.New("benchmarkTmpl").Parse(benchmarkTmplText))
)

type benchmark struct {
	Path, Name, Type, EventProtocolType, EchoCallProtocolType string
	ValueBuild, ValueVar                                      string
	HandleDefs                                                string
	EnableSendEventBenchmark, EnableEchoCallBenchmark         bool
}

type benchmarkTmplInput struct {
	FidlLibrary string
	FidlInclude string
	Benchmarks  []benchmark
}

// Generate generates C++ natural type benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	tmplInput := benchmarkTmplInput{
		FidlLibrary: libraryName(config.CppBenchmarksFidlLibrary),
		FidlInclude: libraryInclude(config.CppBenchmarksFidlLibrary),
	}
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		if gidlir.ContainsUnknownField(gidlBenchmark.Value) {
			continue
		}
		valBuild, valVar := BuildValue(gidlBenchmark.Value, decl, HandleReprRaw)
		tmplInput.Benchmarks = append(tmplInput.Benchmarks, benchmark{
			Path:                     gidlBenchmark.Name,
			Name:                     benchmarkName(gidlBenchmark.Name),
			Type:                     benchmarkTypeFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value),
			EventProtocolType:        benchmarkProtocolFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EventProtocol",
			EchoCallProtocolType:     benchmarkProtocolFromValue(config.CppBenchmarksFidlLibrary, gidlBenchmark.Value) + "EchoCall",
			ValueBuild:               valBuild,
			ValueVar:                 valVar,
			HandleDefs:               libhlcpp.BuildHandleDefs(gidlBenchmark.HandleDefs),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
	}
	var buf bytes.Buffer
	if err := benchmarkTmpl.Execute(&buf, tmplInput); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func libraryInclude(librarySuffix string) string {
	return fmt.Sprintf("fidl/test.benchmarkfidl%s/cpp/natural_messaging.h", strings.ReplaceAll(librarySuffix, " ", ""))
}
func libraryName(librarySuffix string) string {
	return fmt.Sprintf("test_benchmarkfidl%s", strings.ReplaceAll(librarySuffix, " ", ""))
}

func benchmarkTypeFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkProtocolFromValue(librarySuffix string, value gidlir.Value) string {
	return fmt.Sprintf("%s::%s", libraryName(librarySuffix), gidlir.TypeFromValue(value))
}

func benchmarkName(gidlName string) string {
	return strings.ReplaceAll(gidlName, "/", "_")
}
