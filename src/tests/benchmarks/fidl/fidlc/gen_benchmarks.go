// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package main

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strings"
	"text/template"
)

var fileTmpl = template.Must(template.New("fileTmpl").Parse(
	`// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated. To regenerate, run:
// $FUCHSIA_DIR/src/tests/benchmarks/fidl/fidlc/regen.sh

struct Benchmark {
	const char* name;
	const char* fidl;
  };

constexpr Benchmark benchmarks[] = {
  {{- range .Benchmarks }}
  Benchmark{
    .name = "{{ .Name }}",
    .fidl = R"FIDL(
{{ .FIDL }}
)FIDL",
  },
  {{- end }}
};
`))

// file is the input to the file template.
type file struct {
	Benchmarks []fileBenchmark
}

// fileBenchmark is a benchmark in the file template.
type fileBenchmark struct {
	Name string
	FIDL string
}

// benchmarkTemplate is a template for a particular kind of benchmark.
//
// FIDLTemplate is provided a slice of elements with needed template fields
// e.g. I and IPlusOne
type benchmarkTemplate struct {
	Name         string
	FIDLTemplate *template.Template
}

// benchmark is an instance of a benchmarkTemplate.
type benchmark struct {
	Template benchmarkTemplate
	Size     int
}

var structFieldBenchmarkTemplate = benchmarkTemplate{
	Name: "StructField",
	FIDLTemplate: template.Must(template.New("structFieldTmpl").Parse(`
library example;

type TestStruct = struct {
{{ range . -}}
    f{{ .IPlusOne }} int8;
{{ end -}}
};`)),
}

var structDeepBenchmarkTemplate = benchmarkTemplate{
	Name: "StructDeep",
	FIDLTemplate: template.Must(template.New("structDeepTmpl").Parse(`
library example;

type TestStruct0 = struct {
	val int8;
};
{{ range . }}
type TestStruct{{ .IPlusOne }} = struct {
	val TestStruct{{ .I }};
};
{{ end -}}
`)),
}

var tableFieldBenchmarkTemplate = benchmarkTemplate{
	Name: "TableField",
	FIDLTemplate: template.Must(template.New("tableFieldTmpl").Parse(`
library example;

type TestTable = table {
{{ range . -}}
    {{ .IPlusOne }}: f{{ .IPlusOne }} int8;
{{ end -}}
};`)),
}

var tableDeepBenchmarkTemplate = benchmarkTemplate{
	Name: "TableDeep",
	FIDLTemplate: template.Must(template.New("tableDeepTmpl").Parse(`
library example;

type TestTable0 = table {
	1: val int8;
};
{{ range . }}
type TestTable{{ .IPlusOne }} = table {
	1: val TestTable{{ .I }};
};
{{ end -}}
`)),
}

var unionFieldBenchmarkTemplate = benchmarkTemplate{
	Name: "UnionField",
	FIDLTemplate: template.Must(template.New("unionFieldTmpl").Parse(`
library example;

type TestUnion = union {
{{ range . -}}
    {{ .IPlusOne }}: f{{ .IPlusOne }} int8;
{{ end -}}
};`)),
}

var unionDeepBenchmarkTemplate = benchmarkTemplate{
	Name: "UnionDeep",
	FIDLTemplate: template.Must(template.New("unionDeepTmpl").Parse(`
library example;

type TestUnion0 = union {
	1: val int8;
};
{{ range . }}
type TestUnion{{ .IPlusOne }} = union {
	1: val TestUnion{{ .I }};
};
{{ end -}}
`)),
}

var benchmarks = []benchmark{
	{
		Template: structFieldBenchmarkTemplate,
		Size:     64,
	},
	// NOTE: it would be preferable to test larger sizes for StructDeep
	// because it can have poor scaling, but unfortunately larger sizes
	// are too slow to build now. Consider increasing the size when
	// build performance improves.
	{
		Template: structDeepBenchmarkTemplate,
		Size:     8,
	},
	{
		Template: tableFieldBenchmarkTemplate,
		Size:     64,
	},
	{
		Template: tableDeepBenchmarkTemplate,
		Size:     64,
	},
	{
		Template: unionFieldBenchmarkTemplate,
		Size:     64,
	},
	{
		Template: unionDeepBenchmarkTemplate,
		Size:     64,
	},
}

func main() {
	var file file
	for _, b := range benchmarks {
		fb, err := processBenchmark(b)
		if err != nil {
			log.Fatal(err)
		}
		file.Benchmarks = append(file.Benchmarks, fb)
	}

	var buffer bytes.Buffer
	if err := fileTmpl.Execute(&buffer, file); err != nil {
		log.Fatal(err)
	}

	outputFile := os.Args[1]
	if err := ioutil.WriteFile(outputFile, buffer.Bytes(), 0644); err != nil {
		log.Fatal(err)
	}
}

func processBenchmark(b benchmark) (fileBenchmark, error) {
	type benchmarkTmpl struct {
		I        int
		IPlusOne int
	}
	input := make([]benchmarkTmpl, b.Size)
	for i := range input {
		input[i] = benchmarkTmpl{
			I:        i,
			IPlusOne: i + 1,
		}
	}
	var sb strings.Builder
	if err := b.Template.FIDLTemplate.Execute(&sb, input); err != nil {
		return fileBenchmark{}, err
	}

	return fileBenchmark{
		Name: fmt.Sprintf("%s/%d", b.Template.Name, b.Size),
		FIDL: sb.String(),
	}, nil
}
