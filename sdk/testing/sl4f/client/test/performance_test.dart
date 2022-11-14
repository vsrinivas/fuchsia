// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io' show File, Directory;

import 'package:mockito/mockito.dart';
import 'package:path/path.dart' as path;
import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

class MockSl4f extends Mock implements Sl4f {}

class MockDump extends Mock implements Dump {}

class RunProcessObserver {
  void runProcess(String executablePath, List<String> args) {}
}

class MockRunProcessObserver extends Mock implements RunProcessObserver {}

class FakePerformanceTools extends Performance {
  final RunProcessObserver _observer;
  FakePerformanceTools(Sl4f _sl4f, Dump _dump, RunProcessObserver observer)
      : _observer = observer,
        super(_sl4f, _dump);

  @override
  Future<bool> runProcess(String executablePath, List<String> args) async {
    _observer.runProcess(executablePath, args);
    return true;
  }
}

const String sl4fTraceRequestMethod = 'traceutil_facade.GetTraceFile';

void main(List<String> args) {
  Dump mockDump;
  Sl4f mockSl4f;
  setUp(() {
    mockDump = MockDump();
    mockSl4f = MockSl4f();
  });

  Directory createTempDir() {
    final tempDir = Directory.systemTemp.createTempSync();
    addTearDown(() {
      tempDir.deleteSync(recursive: true);
    });
    return tempDir;
  }

  // TODO(fxbug.dev/59861): Update this test to the new fuchsia perf json
  // format when the migration is done.
  test('process trace', () async {
    final metricsSpecs = [
      MetricsSpec(name: 'test_metric_1'),
      MetricsSpec(name: 'test_metric_2'),
    ];
    final metricsSpecSet =
        MetricsSpecSet(testName: 'test_name', metricsSpecs: metricsSpecs);
    final testMetricsRegistry = {
      'test_metric_1': (Model model, Map<String, dynamic> extraArgs) => [
            TestCaseResults('label_1', Unit.nanoseconds, [1.0])
          ],
      'test_metric_2': (Model model, Map<String, dynamic> extraArgs) => [
            TestCaseResults('label_2', Unit.milliseconds, [2.0])
          ],
    };

    final traceFile = File(path.join(createTempDir().path, 'sample-trace.json'))
      ..createSync()
      ..writeAsStringSync('''
    {
      "traceEvents": [],
      "systemTraceEvents": {
        "type": "fuchsia",
        "events": []
      }
    }
    ''');

    final performance = Performance(mockSl4f, mockDump);
    final resultsFile = await performance
        .processTrace(metricsSpecSet, traceFile, registry: testMetricsRegistry);

    final resultsFileContents = await resultsFile.readAsString();
    final resultsObject = json.decode(resultsFileContents);

    expect(resultsObject[0]['label'], equals('label_1'));
    expect(resultsObject[0]['test_suite'], equals('test_name'));
    expect(resultsObject[0]['unit'], equals('nanoseconds'));
    expect(resultsObject[0]['values'], equals([1.0]));

    expect(resultsObject[1]['label'], equals('label_2'));
    expect(resultsObject[1]['test_suite'], equals('test_name'));
    expect(resultsObject[1]['unit'], equals('milliseconds'));
    expect(resultsObject[1]['values'], equals([2.0]));
  });

  test('process trace with testSuite set but not testName', () async {
    final metricsSpecs = [
      MetricsSpec(name: 'test_metric_1'),
      MetricsSpec(name: 'test_metric_2'),
    ];
    final metricsSpecSet =
        MetricsSpecSet(testSuite: 'test_suite', metricsSpecs: metricsSpecs);
    final testMetricsRegistry = {
      'test_metric_1': (Model model, Map<String, dynamic> extraArgs) => [
            TestCaseResults('label_1', Unit.nanoseconds, [1.0])
          ],
      'test_metric_2': (Model model, Map<String, dynamic> extraArgs) => [
            TestCaseResults('label_2', Unit.milliseconds, [2.0])
          ],
    };

    final traceFile = File(path.join(createTempDir().path, 'sample-trace.json'))
      ..createSync()
      ..writeAsStringSync('''
    {
      "traceEvents": [],
      "systemTraceEvents": {
        "type": "fuchsia",
        "events": []
      }
    }
    ''');

    final performance = Performance(mockSl4f, mockDump);
    final resultsFile = await performance
        .processTrace(metricsSpecSet, traceFile, registry: testMetricsRegistry);

    final resultsFileContents = await resultsFile.readAsString();
    final resultsObject = json.decode(resultsFileContents);

    expect(resultsObject[0]['label'], equals('label_1'));
    expect(resultsObject[0]['test_suite'], equals('test_suite'));
    expect(resultsObject[0]['unit'], equals('nanoseconds'));
    expect(resultsObject[0]['values'], equals([1.0]));

    expect(resultsObject[1]['label'], equals('label_2'));
    expect(resultsObject[1]['test_suite'], equals('test_suite'));
    expect(resultsObject[1]['unit'], equals('milliseconds'));
    expect(resultsObject[1]['values'], equals([2.0]));
  });

  test('convert trace', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);
    final convertedTraces = [
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt')),
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt.gz')),
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt'),
          compressedOutput: true),
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt.gz'),
          compressedOutput: true),
      // These compressedInput values intentionally do not match the file
      // extensions to test that the code isn't ignoring the compressedInput
      // argument.
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt'),
          compressedInput: true, compressedOutput: true),
      await performance.convertTraceFileToJson(
          '/path/to/trace2json', File('/path/to/trace.fxt.gz'),
          compressedInput: false, compressedOutput: true),
    ];
    expect(convertedTraces[0].path, '/path/to/trace.json');
    expect(convertedTraces[1].path, '/path/to/trace.json');
    expect(convertedTraces[2].path, '/path/to/trace.json.gz');
    expect(convertedTraces[3].path, '/path/to/trace.json.gz');
    expect(convertedTraces[4].path, '/path/to/trace.json.gz');
    expect(convertedTraces[5].path, '/path/to/trace.json.gz');

    final verifyMockRunProcessObserver = verify(mockRunProcessObserver
        .runProcess(argThat(endsWith('trace2json')), captureAny))
      ..called(6);
    final capturedArgs = verifyMockRunProcessObserver.captured;
    expect(capturedArgs[0], [
      '--input-file=/path/to/trace.fxt',
      '--output-file=/path/to/trace.json'
    ]);
    expect(capturedArgs[1], [
      '--input-file=/path/to/trace.fxt.gz',
      '--output-file=/path/to/trace.json',
      '--compressed-input'
    ]);
    expect(capturedArgs[2], [
      '--input-file=/path/to/trace.fxt',
      '--output-file=/path/to/trace.json.gz',
      '--compressed-output'
    ]);
    expect(capturedArgs[3], [
      '--input-file=/path/to/trace.fxt.gz',
      '--output-file=/path/to/trace.json.gz',
      '--compressed-input',
      '--compressed-output'
    ]);
    expect(capturedArgs[4], [
      '--input-file=/path/to/trace.fxt',
      '--output-file=/path/to/trace.json.gz',
      '--compressed-input',
      '--compressed-output'
    ]);
    expect(capturedArgs[5], [
      '--input-file=/path/to/trace.fxt.gz',
      '--output-file=/path/to/trace.json.gz',
      '--compressed-output'
    ]);
  });
}
