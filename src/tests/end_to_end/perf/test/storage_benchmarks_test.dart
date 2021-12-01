// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

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
    final result = results[event.name] ??= TestCaseResults(
      event.name,
      Unit.milliseconds,
      [],
    );
    result.values.add(event.duration.toMillisecondsF());
  }

  return results.values.toList();
}

Future<void> runOdu(PerfTestHelper helper, int fileSize, int ioSize,
    String operation, String cleanup, String target) async {
  // Only read/write the entire file once. Most filesystems cache reads and
  // writes in memory so quickly hitting the same block multiple times would
  // be entirely served from memory and water down the results.
  expect(fileSize % ioSize, equals(0));
  final operationCount = fileSize ~/ ioSize;
  final result = await helper.component.launch(_appPath, [
    '--target=$target',
    '--target_length=$fileSize',
    '--operations=$operation',
    '--max_io_count=$operationCount',
    '--block_size=$ioSize',
    '--max_io_size=$ioSize',
    '--sequential=true',
    '--log_ftrace=true',
    '--align=true',
    '--thread_count=1',
    '--cleanup=$cleanup'
  ]);
  if (result != 'Success') {
    throw Exception('Failed to launch $_appPath.');
  }
}

void _addTest(String filesystem, String directory) {
  final testName = 'fuchsia.storage.benchmarks.$filesystem';
  final filename = '$directory/odu-sequential-block-aligned';
  const fileSize = 10 * 1024 * 1024;
  const ioSize = 8192;

  test(testName, () async {
    final helper = await PerfTestHelper.make();

    final traceSession =
        await helper.performance.initializeTracing(categories: ['benchmark']);
    await traceSession.start();

    // Run sequential write perf tests.
    await runOdu(helper, fileSize, ioSize, 'write', 'false', filename);

    // Run sequential read perf tests on the same set of files created by write tests above.
    await runOdu(helper, fileSize, ioSize, 'read', 'true', filename);

    // TODO(fxbug.dev/54931): Explicitly stop tracing.
    // await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    await helper.performance.processTrace(
      MetricsSpecSet(
        metricsSpecs: [MetricsSpec(name: 'storage')],
        testName: testName,
      ),
      jsonTraceFile,
      converterPath: _catapultConverterPath,
      registry: {
        'storage': _storageBenchmarksMetricsProcessor,
      },
    );
  }, timeout: Timeout.none);
}

void main() {
  enableLoggingOutput();
  _addTest('minfs', '/data');
  _addTest('memfs', '/tmp');
}
