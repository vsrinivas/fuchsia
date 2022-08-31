// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _trace2jsonPath = 'runtime_deps/trace2json';

void main() {
  enableLoggingOutput();

  // This test checks that the perftest library produces the expected set
  // of trace events when tracing is enabled.
  //
  // It also indirectly tests for a race condition in trace provider
  // startup (https://fxbug.dev/22911) in which early trace events can be
  // lost.
  const testName = 'perftest_library_trace_events_test';
  test(testName, () async {
    final helper = await PerfTestHelper.make();

    final traceSession = await helper.performance
        .initializeTracing(categories: ['kernel', 'perftest'], bufferSize: 36);
    await traceSession.start();

    await helper.runTestComponentWithNoResults(
        packageName: 'fuchsia_microbenchmarks_perftestmode',
        componentName: 'fuchsia_microbenchmarks_perftestmode.cm',
        commandArgs: '-p --quiet'
            ' --runs 4 --enable-tracing --filter="^Null\$"');

    await traceSession.stop();

    final fxtTraceFile = await traceSession.terminateAndDownload(testName);
    final jsonTraceFile = await helper.performance
        .convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);

    final Model model = await createModelFromFile(jsonTraceFile);
    final events = filterEventsTyped<DurationEvent>(getAllEvents(model),
        category: 'perftest');
    expect(events.map((event) => event.name), [
      'test_group',
      'test_setup',
      'test_run',
      'test_run',
      'test_run',
      'test_run',
      'test_teardown'
    ]);
  }, timeout: Timeout.none);
}
