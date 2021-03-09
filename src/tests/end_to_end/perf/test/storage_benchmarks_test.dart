// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _testName = 'fuchsia.storage.benchmarks';
const _appPath = 'fuchsia-pkg://fuchsia.com/odu#meta/odu.cmx';
const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _trace2jsonPath = 'runtime_deps/trace2json';

/// Custom MetricsProcessor that generates one TestCaseResults for each unique
/// event name in the model with category 'benchmark'.
///
/// This avoids creating a MetricsSpec for each event separately.
List<TestCaseResults> _storageBenchmarksMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final allEvents = filterEventsTyped<DurationEvent>(getAllEvents(model),
      category: 'benchmark');

  final Map<String, TestCaseResults> results = {};

  for (final event in allEvents) {
    results[event.name] ??= TestCaseResults(
      event.name,
      Unit.milliseconds,
      [],
    );
    results[event.name].values.add(event.duration.toMillisecondsF());
  }

  return results.values.toList();
}

Future<void> runStorageTest(
    PerfTestHelper helper, int blockSize, int maxIoSize, String target) async {
  final result = await helper.component.launch(_appPath, [
    '--operations=write',
    '--target=$target',
    '--max_io_count=1000',
    '--sequential=true',
    '--block_size=$blockSize',
    '--log_ftrace=true',
    '--max_io_size=$maxIoSize',
    '--align=true'
  ]);
  if (result != 'Success') {
    throw Exception('Failed to launch $_appPath.');
  }
}

void main() {
  enableLoggingOutput();

  test(_testName, () async {
    final helper = await PerfTestHelper.make();

    final traceSession =
        await helper.performance.initializeTracing(categories: ['benchmark']);
    await traceSession.start();
    await runStorageTest(
        helper, 8192, 8192, '/data/odu-sequential-block-aligned');
    await runStorageTest(helper, 1, 512, '/data/odu-sequential-unaligned');

    // TODO(fxbug.dev/54931): Explicitly stop tracing.
    // await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(_testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    await helper.performance.processTrace(
      MetricsSpecSet(
        metricsSpecs: [MetricsSpec(name: 'storage')],
        testName: _testName,
      ),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: {
        'storage': _storageBenchmarksMetricsProcessor,
      },
    );
  }, timeout: Timeout.none);
}
