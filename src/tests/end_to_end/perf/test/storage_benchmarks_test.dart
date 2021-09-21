// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

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

Future<void> runStorageTest(PerfTestHelper helper, int blockSize, int maxIoSize,
    String operation, String cleanup, String target) async {
  final result = await helper.component.launch(_appPath, [
    '--operations=$operation',
    '--target=$target',
    '--max_io_count=500',
    '--sequential=true',
    '--block_size=$blockSize',
    '--log_ftrace=true',
    '--max_io_size=$maxIoSize',
    '--align=true',
    '--cleanup=$cleanup'
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

    // Run sequential write perf tests.
    await runStorageTest(helper, 8192, 8192, 'write', 'false',
        '/data/odu-sequential-block-aligned');
    await runStorageTest(
        helper, 1, 512, 'write', 'false', '/data/odu-sequential-unaligned');

    // Run sequential read perf tests on the same set of files created by write tests above.
    await runStorageTest(helper, 8192, 8192, 'read', 'true',
        '/data/odu-sequential-block-aligned');
    await runStorageTest(
        helper, 1, 512, 'read', 'true', '/data/odu-sequential-unaligned');

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
