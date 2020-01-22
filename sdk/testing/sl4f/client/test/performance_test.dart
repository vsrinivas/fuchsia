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

  // Verifies that [Performance.downloadTraceFile] requests a file from Sl4f and
  // writes its contents to a local file.
  test('download trace requests a file and writes it', () async {
    final performance = Performance(mockSl4f, mockDump);
    final List<int> traceData =
        utf8.encode('{"traceEvents": [], "displayTimeUnit": "ns"}');
    when(mockSl4f.request(sl4fTraceRequestMethod, any))
        .thenAnswer((_) => Future.value({'data': base64.encode(traceData)}));

    await performance.downloadTraceFile('test-trace');

    // Verify Sl4f.request.
    final verifyMockSl4fRequest =
        verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))..called(1);
    expect(verifyMockSl4fRequest.captured.single,
        {'path': '/tmp/test-trace-trace.json'});

    // Verify Dump.writeAsBytes.
    final verifyMockDumpWriteAsBytes =
        verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
          ..called(1);
    expect(verifyMockDumpWriteAsBytes.captured,
        ['test-trace-trace', 'json', traceData]);
  });

  test('download trace handles non-utf8 data', () async {
    final performance = Performance(mockSl4f, mockDump);

    final List<int> nonUtf8Data = [
      0x1f,
      0x8b,
      0x08,
      0x08,
      0x35,
      0xdc,
      0x00,
      0x03
    ];

    expect(() => utf8.decode(nonUtf8Data), throwsException);

    when(mockSl4f.request(sl4fTraceRequestMethod, any))
        .thenAnswer((_) => Future.value({'data': base64.encode(nonUtf8Data)}));

    await performance.downloadTraceFile('non-utf8');

    final verifyMockSl4fRequest =
        verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))..called(1);
    expect(verifyMockSl4fRequest.captured.single,
        {'path': '/tmp/non-utf8-trace.json'});

    final verifyMockDumpWriteAsBytes =
        verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
          ..called(1);
    expect(verifyMockDumpWriteAsBytes.captured,
        ['non-utf8-trace', 'json', nonUtf8Data]);
  });

  test('process trace', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);

    // Test trace processing with [appName] set.
    await performance.processTrace(
        '/bin/process_sample_trace', File('sample-trace.json'), 'test1',
        appName: 'test-app');

    var verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        '/bin/process_sample_trace', captureAny))
      ..called(1);
    var capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '-test_suite_name=test1');
    expect(capturedArgs[1], '-flutter_app_name=test-app');
    expect(capturedArgs[2], endsWith('test1-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[3], endsWith('sample-trace.json'));

    // Test trace processing without an [appName] set.
    await performance.processTrace(
        '/bin/process_sample_trace', File('sample-trace.json'), 'test1');

    verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        '/bin/process_sample_trace', captureAny))
      ..called(1);
    capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '-test_suite_name=test1');
    expect(capturedArgs[1], endsWith('test1-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[2], endsWith('sample-trace.json'));
  });

  Directory createTempDir() {
    final tempDir = Directory.systemTemp.createTempSync();
    addTearDown(() {
      tempDir.deleteSync(recursive: true);
    });
    return tempDir;
  }

  test('process trace 2', () async {
    final metricsSpecs = [
      MetricsSpec(name: 'test_metric_1'),
      MetricsSpec(name: 'test_metric_2'),
    ];
    final metricsSpecSet =
        MetricsSpecSet(testName: 'test_name', metricsSpecs: metricsSpecs);
    final testMetricsRegistry = {
      'test_metric_1': (Model model, MetricsSpec metricsSpec) => [
            TestCaseResults('label_1', Unit.nanoseconds, [1.0])
          ],
      'test_metric_2': (Model model, MetricsSpec metricsSpec) => [
            TestCaseResults('label_2', Unit.milliseconds, [2.0])
          ]
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
    final resultsFile = await performance.processTrace2(
        metricsSpecSet, traceFile,
        registry: testMetricsRegistry);

    final resultsFileContents = await resultsFile.readAsString();
    final resultsObject = json.decode(resultsFileContents);

    expect(resultsObject[0]['label'], equals('label_1'));
    expect(resultsObject[0]['test_suite'], equals('test_name'));
    expect(resultsObject[0]['unit'], equals('nanoseconds'));
    expect(resultsObject[0]['values'], equals([1.0]));
    expect(resultsObject[0]['split_first'], equals(false));

    expect(resultsObject[1]['label'], equals('label_2'));
    expect(resultsObject[1]['test_suite'], equals('test_name'));
    expect(resultsObject[1]['unit'], equals('milliseconds'));
    expect(resultsObject[1]['values'], equals([2.0]));
    expect(resultsObject[1]['split_first'], equals(false));
  });

  test('convert results', () async {
    final mockRunProcessObserver = MockRunProcessObserver();
    final performance =
        FakePerformanceTools(mockSl4f, mockDump, mockRunProcessObserver);

    // With no buildbucket id env variable, it should do a local run.
    File convertedFile = await performance.convertResults(
        '/bin/catapult_converter',
        File('test1-benchmark.fuchsiaperf.json'), {});
    expect(convertedFile.path, endsWith('test1-benchmark.catapult_json'));
    var verifyMockRunProcessObserver = verify(mockRunProcessObserver.runProcess(
        argThat(endsWith('catapult_converter')), captureAny))
      ..called(1);
    var capturedArgs = verifyMockRunProcessObserver.captured.single;
    expect(capturedArgs[0], '--input');
    expect(capturedArgs[1], endsWith('test1-benchmark.fuchsiaperf.json'));
    expect(capturedArgs[2], '--output');
    expect(capturedArgs[3], endsWith('test1-benchmark.catapult_json'));
    expect(capturedArgs[4], '--execution-timestamp-ms');
    expect(int.parse(capturedArgs[5]) != null, true);
    expect(capturedArgs[6], '--masters');
    expect(capturedArgs[7], 'local-master');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'http://ci.example.com/build/300');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'local-bot');

    // Otherwise, it should do a bot run.
    var environment = {
      'FUCHSIA_BUILD_DIR': 'out/default',
      'BUILDER_NAME': 'fuchsia-builder',
      'BUILDBUCKET_ID': '8abc123',
      'BUILD_CREATE_TIME': '1561234567890',
      'INPUT_COMMIT_HOST': 'myhost.googlesource.com',
      'INPUT_COMMIT_PROJECT': 'project1',
      'INPUT_COMMIT_REF': 'refs/heads/master'
    };

    convertedFile = await performance.convertResults('/bin/catapult_converter',
        File('test2-benchmark.fuchsiaperf.json'), environment);
    expect(convertedFile.path, endsWith('test2-benchmark.catapult_json'));
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
    expect(capturedArgs[7], 'myhost.project1');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'https://ci.chromium.org/b/8abc123');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'fuchsia-builder');

    // If head is not on master, then it should be appended to master name.
    environment['INPUT_COMMIT_REF'] = 'refs/heads/releases/rc1';
    convertedFile = await performance.convertResults('/bin/catapult_converter',
        File('test3-benchmark.fuchsiaperf.json'), environment);
    expect(convertedFile.path, endsWith('test3-benchmark.catapult_json'));
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
    expect(capturedArgs[7], 'myhost.project1.rc1');
    expect(capturedArgs[8], '--log-url');
    expect(capturedArgs[9], 'https://ci.chromium.org/b/8abc123');
    expect(capturedArgs[10], '--bots');
    expect(capturedArgs[11], 'fuchsia-builder');
  });

  test('trace options', () async {
    final List<int> data = [0x10, 0x00, 0x04, 0x46, 0x78, 0x54, 0x16, 0x00];

    Future<void> doTest(String expectedExtension,
        {bool binary, bool compress}) async {
      final mockDump = MockDump();
      final mockSl4f = MockSl4f();

      when(mockSl4f.request(sl4fTraceRequestMethod, any))
          .thenAnswer((_) => Future.value({'data': base64.encode(data)}));

      final performance = Performance(mockSl4f, mockDump);
      Map<Symbol, dynamic> traceOptions = {};
      if (binary != null) {
        traceOptions[#binary] = binary;
      }
      if (compress != null) {
        traceOptions[#compress] = compress;
      }
      await Function.apply(
          performance.downloadTraceFile, ['options'], traceOptions);

      final verifyMockSl4fRequest =
          verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))
            ..called(1);
      expect(verifyMockSl4fRequest.captured.single,
          {'path': '/tmp/options-trace.$expectedExtension'});

      final verifyMockDumpWriteAsBytes =
          verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
            ..called(1);
      expect(verifyMockDumpWriteAsBytes.captured,
          ['options-trace', expectedExtension, data]);
    }

    await doTest('json');
    await doTest('json', binary: false, compress: false);
    await doTest('json.gz', compress: true);
    await doTest('json.gz', binary: false, compress: true);
    await doTest('fxt', binary: true);
    await doTest('fxt', binary: true, compress: false);
    await doTest('fxt.gz', binary: true, compress: true);
  });

  test('chunked trace download', () async {
    final performance = Performance(mockSl4f, mockDump);

    final List<int> largeData = List.filled(0x150000, 0);
    const chunkSize = 0x100000;

    var responses = [
      {
        'data': base64.encode(largeData.sublist(0, chunkSize)),
        'next_offset': chunkSize,
      },
      {
        'data': base64.encode(largeData.sublist(chunkSize)),
      }
    ];

    when(mockSl4f.request(sl4fTraceRequestMethod, any))
        .thenAnswer((_) => Future.value(responses.removeAt(0)));

    await performance.downloadTraceFile('chunked');

    final verifyMockSl4fRequest =
        verify(mockSl4f.request(sl4fTraceRequestMethod, captureAny))..called(2);
    expect(verifyMockSl4fRequest.captured, [
      {'path': '/tmp/chunked-trace.json'},
      {'path': '/tmp/chunked-trace.json', 'offset': chunkSize}
    ]);

    final verifyMockDumpWriteAsBytes =
        verify(mockDump.writeAsBytes(captureAny, captureAny, captureAny))
          ..called(1);
    expect(verifyMockDumpWriteAsBytes.captured,
        ['chunked-trace', 'json', largeData]);
  });
}
