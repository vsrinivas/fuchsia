// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File, Directory;

import 'package:mockito/mockito.dart';
import 'package:path/path.dart' as path;
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

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

  test('convert results', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);

    // With no buildbucket id env variable, it should do a local run.
    await performance.convertResults('/bin/catapult_converter',
        File('test1-benchmark.fuchsiaperf.json'), {});
    var verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        argThat(endsWith('catapult_converter')), captureAny))
      ..called(1);
    var capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '--input');
    expect(capturedArgs[1], endsWith('test1-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[2], '--output');
    expect(capturedArgs[3], endsWith('test1-benchmark.catapult_json_disabled'));
    expect(capturedArgs[4], '--execution-timestamp-ms');
    expect(int.parse(capturedArgs[5]) != null, true);
    expect(capturedArgs[6], '--masters');
    expect(capturedArgs[7], 'local-master');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'http://ci.example.com/build/300');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'local-bot');

    // Otherwise, it should do a bot run.
    const environment = {
      'CATAPULT_DASHBOARD_MASTER': 'example.fuchsia.global.ci',
      'CATAPULT_DASHBOARD_BOT': 'example-fuchsia-x64-nuc',
      'BUILDBUCKET_ID': '8abc123',
      'BUILD_CREATE_TIME': '1561234567890',
    };

    await performance.convertResults('/bin/catapult_converter',
        File('test2-benchmark.fuchsiaperf.json'), environment);
    verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        argThat(endsWith('catapult_converter')), captureAny))
      ..called(1);
    capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '--input');
    expect(capturedArgs[1], endsWith('test2-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[2], '--output');
    expect(capturedArgs[3], endsWith('test2-benchmark.catapult_json'));
    expect(capturedArgs[4], '--execution-timestamp-ms');
    expect(capturedArgs[5], '1561234567890');
    expect(capturedArgs[6], '--masters');
    expect(capturedArgs[7], 'example.fuchsia.global.ci');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'https://ci.chromium.org/b/8abc123');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'example-fuchsia-x64-nuc');

    // If it is a bot run with release version, should have the product-versions arg.
    const environmentWithVersion = {
      'CATAPULT_DASHBOARD_MASTER': 'example.fuchsia.global.ci',
      'CATAPULT_DASHBOARD_BOT': 'example-fuchsia-x64-nuc',
      'BUILDBUCKET_ID': '8abc123',
      'BUILD_CREATE_TIME': '1561234567890',
      'RELEASE_VERSION': '0.001.20.3',
    };

    await performance.convertResults('/bin/catapult_converter',
        File('test3-benchmark.fuchsiaperf.json'), environmentWithVersion);
    verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        argThat(endsWith('catapult_converter')), captureAny))
      ..called(1);
    capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '--input');
    expect(capturedArgs[1], endsWith('test3-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[2], '--output');
    expect(capturedArgs[3], endsWith('test3-benchmark.catapult_json'));
    expect(capturedArgs[4], '--execution-timestamp-ms');
    expect(capturedArgs[5], '1561234567890');
    expect(capturedArgs[6], '--masters');
    expect(capturedArgs[7], 'example.fuchsia.global.ci');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'https://ci.chromium.org/b/8abc123');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'example-fuchsia-x64-nuc');
    expect(capturedArgs[12], '--product-versions');
    expect(capturedArgs[13], '0.001.20.3');
  });

  // convertResults() should raise an error if a non-empty subset of the
  // infra env vars are set.
  test('convertResults check env vars', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);

    final environment = {
      'CATAPULT_DASHBOARD_MASTER': 'example.fuchsia.global.ci',
    };
    expect(
        performance.convertResults('/bin/catapult_converter',
            File('test1-benchmark.fuchsiaperf.json'), environment),
        throwsA(TypeMatcher<ArgumentError>()));
  });

  // convertResults() should raise an error when given a filename without the
  // proper filename extension.
  test('convertResults check filename extension', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);

    expect(
        performance.convertResults(
            '/bin/catapult_converter', File('results-fuchsiaperf.json'), {}),
        throwsA(TypeMatcher<ArgumentError>()));
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
