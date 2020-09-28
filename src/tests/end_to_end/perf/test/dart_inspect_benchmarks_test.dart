// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';
import 'package:logging/logging.dart';

import 'helpers.dart';

const _testName = 'fuchsia.dart_inspect.basic_benchmarks';
const _appName = 'dart-inspect-benchmarks';
const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';
const _iterations = 1000;

final _log = Logger('DartInspectBenchmarksTest');

/// Mapping of event names to output test names.
/// These event names must be kept in sync with
/// //topaz/tests/dart-inspect-benchmarks/lib/dart_inspect_benchmarks.dart.
const _eventNameMap = {
  'Inc integer': 'basic/increment',
  'Set integer': 'basic/setInt',
  'Delete node': 'basic/delNode',
  'Add child': 'basic/addNode',
  'Set byte': 'basic/setByte',
  'Reset byte': 'basic/resetByte',
  'Set byte long': 'basic/setByteLong',
  'Reset byte long': 'basic/resetByteLong',
  'Set string': 'basic/setString',
  'Reset string': 'basic/resetString',
  'Set string long': 'basic/setStringLong',
  'Reset string long': 'basic/resetStringLong',
  'Inc double': 'basic/incDouble',
  'Set double': 'basic/setDouble',
  'Allocation': 'basic/alloc',
  'URL sized string': 'basic/url',
};

/// Custom [MetricsProcessor] that generates one [TestCaseResults] for each
/// event name in [_eventNameMap].
///
/// TODO(fxbug.dev/53934): Share metrics processor with other perf tests.
List<TestCaseResults> _metricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final allEvents = filterEventsTyped<DurationEvent>(getAllEvents(model),
      category: 'dart:dart');

  final Map<String, TestCaseResults> results = {};

  for (final event in allEvents) {
    // Add events to the map even if their name is not a key in _eventNameMap.
    // This keeps track of any "unexpected" events, which are logged below.
    results[event.name] ??= TestCaseResults(
      _eventNameMap[event.name],
      Unit.milliseconds,
      [],
    );
    results[event.name].values.add(event.duration.toMillisecondsF());
  }

  // Note any events that were found in the model but not in the event name map.
  // These messages should be useful when updating or debugging the benchmark.
  for (final eventName
      in results.keys.where((name) => results[name].label == null)) {
    _log.fine('Got ${results[eventName].values.length} "$eventName" events, '
        'but this event name is not present in the map. Avg: '
        '${computeMean<double>(results[eventName].values).toStringAsFixed(3)} '
        'ms');
  }

  for (final eventName in _eventNameMap.keys) {
    expect(results[eventName]?.values?.length, _iterations,
        reason: 'Event name: "$eventName"');
  }

  return results.values.where((result) => result.label != null).toList();
}

void main() {
  enableLoggingOutput();

  test(_testName, () async {
    final helper = await PerfTestHelper.make();
    final stopwatch = Stopwatch();

    final traceSession =
        await helper.performance.initializeTracing(categories: ['dart:dart']);
    await traceSession.start();

    _log.info('Running: dart-inspect-benchmarks --iterations=$_iterations');
    stopwatch.start();
    await helper.component.launch(_appName, ['--iterations=$_iterations']);
    stopwatch.stop();
    _log.info('Completed $_iterations iterations in '
        '${stopwatch.elapsed.inSeconds} seconds.');

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    await helper.performance.processTrace(
      MetricsSpecSet(
        metricsSpecs: [MetricsSpec(name: 'dart_inspect')],
        testName: _testName,
      ),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: {
        'dart_inspect': _metricsProcessor,
      },
    );
  }, timeout: Timeout.none);
}
