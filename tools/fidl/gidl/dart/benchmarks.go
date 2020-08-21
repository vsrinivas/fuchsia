// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dart

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:convert';
import 'dart:io' hide exit;
import 'dart:typed_data';

import 'package:fuchsia/fuchsia.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

import 'package:fidl/fidl.dart';
import 'package:fidl_benchmarkfidl/fidl_async.dart';

typedef _CallbackSetter = void Function(void Function());
typedef _DefinitionBlock = void Function(
    _CallbackSetter run, _CallbackSetter teardown);

class _Definition {
  final String name;
  final _DefinitionBlock block;

  void Function() _run;
  void Function() _teardown;

  _Definition(this.name, this.block);

  double execute() {
    block((void Function() run) => _run = run,
        (void Function() teardown) => _teardown = teardown);
    if (_run == null) {
      throw Exception("Benchmark $name doesn't declare what to run.");
    }

    // Warmup for at least 100ms. Discard result.
    _measure(100);

    // Run the benchmark for at least 1s.
    double result = _measure(1000);

    if (_teardown != null) {
      _teardown();
    }

    print('$name $result ms\n');
    return result;
  }

  // Measures the score for this benchmark by executing it repeateldy until
  // time minimum has been reached.
  double _measure(int minimumMillis) {
    int minimumMicros = minimumMillis * 1000;
    int iter = 0;
    Stopwatch watch = Stopwatch()..start();
    int elapsed = 0;
    while (elapsed < minimumMicros) {
      _run();
      elapsed = watch.elapsedMicroseconds;
      iter++;
    }
    return elapsed / iter;
  }
}

class BenchmarkGroup {
  final List<_Definition> definitions;
  final String name;

  BenchmarkGroup(this.name) : definitions = [];

  void benchmark(final String name, final _DefinitionBlock block) {
    definitions.add(_Definition(name, block));
  }

  List<TestCaseResults> run() {
    final List<TestCaseResults> results = [];
    for (final def in definitions) {
      final result = def.execute();
      results.add(TestCaseResults(def.name, Unit.nanoseconds, [result * 1000]));
    }
    return results;
  }
}

{{ range .Benchmarks }}
void encode{{ .Name }}Benchmark(run, teardown) {
	final value = {{ .Value }};
	run(() {
		final Encoder encoder = Encoder()
			..alloc({{ .ValueType}}.inlineSize);
		{{ .ValueType }}.encode(encoder, value, 0);
	});
}
void decode{{ .Name }}Benchmark(run, teardown) {
	final value = {{ .Value }};
	final Encoder encoder = Encoder()..alloc({{ .ValueType}}.inlineSize);
	{{ .ValueType }}.encode(encoder, value, 0);
	run(() {
		final Decoder decoder = Decoder(encoder.message)
			..claimMemory({{ .ValueType}}.inlineSize);
			{{ .ValueType }}.decode(decoder, 0);
	});
}
{{ end }}

List<Map<String, dynamic>> resultsToJson(
    List<TestCaseResults> results, String suiteName) {
  return results.map((result) => result.toJson(testSuite: suiteName)).toList();
}

void main() async {
  test('main', () async {
    final benchmarks = BenchmarkGroup('fuchsia.fidl_microbenchmarks')
    {{ range .Benchmarks }}
      ..benchmark('Dart/Encode/{{ .ChromeperfPath }}/WallTime', encode{{.Name}}Benchmark)
      ..benchmark('Dart/Decode/{{ .ChromeperfPath }}/WallTime', decode{{.Name}}Benchmark)
    {{ end }};

    final results = benchmarks.run();
    if (results.isEmpty) {
      exit(1);
    }

    var encoder = JsonEncoder.withIndent('    ');
    await File('/data/results.json').writeAsString(
      encoder.convert(resultsToJson(results, benchmarks.name)),
    mode: FileMode.write);
  }, timeout: Timeout(Duration(minutes: 10)));
}
`))

type benchmarkTmplInput struct {
	Benchmarks []benchmark
}

type benchmark struct {
	Name, ChromeperfPath, Value, ValueType string
}

// GenerateBenchmarks generates Dart benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		valueType := typeName(decl)
		benchmarks = append(benchmarks, benchmark{
			Name:           benchmarkName(gidlBenchmark.Name),
			ChromeperfPath: gidlBenchmark.Name,
			Value:          value,
			ValueType:      valueType,
		})
	}
	input := benchmarkTmplInput{
		Benchmarks: benchmarks,
	}
	var buf bytes.Buffer
	err := benchmarkTmpl.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func benchmarkName(gidlName string) string {
	return fidlcommon.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
