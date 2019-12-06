// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Main entry-point for all Fuchsia tests, both host and on-device.
///
/// [FuchsiaTestCommand] receives a combination of test names and feature flags,
/// and due to the nature of Fuchsia tests, needs to run each passed test name
/// against the full set of feature flags. For example, if the way to invoke
/// this command is `fx test <...args>`, and the developer executes
/// `... test1 test2 -flag1 -flag2`, the desired behavior for our user lies
/// behind pretending the user entered `... test1 -flag1 -flag2` and
/// `... test2 -flag1 flag2` separately. Note that the individual tests indicated
/// by these two imaginary commands are then rolled back into one singular test
/// suite, so end users will be none the wiser.
///
/// The following is a high level road map of how [FuchsiaTestCommand] works:
///
///  - Parse commands into testNames and flags
///  - Produce a list of each individual `testName` paired with all provided flags
///     - loop over this list `testName` and flag combinations, matching tests
///       out of `out/default/tests.json`
///     - Load each matching test's raw JSON into a [TestDefinition] instance which
///       handles determining all the runtime invocation details for a given test
///  - Loop over the list of [TestDefinition] instances, listening to emitted
///    [TestEvent] instances with a [FormattedOutput] class
///     - Every time a [TestEvent] instance is emitted, our [FormattedOutput]
///       class captures it and flushes appropriate text to [stdout].
///  - Once the [TestDefinition] list has been completely processed, flush any
///    captured stderr from individual test processes to show errors / stacktraces
class FuchsiaTestCommand {
  // ignore: close_sinks
  final _eventStreamController = StreamController<TestEvent>();

  /// Bundle of configuration options for this invocation.
  final TestFlags testFlags;

  /// Absolute path of the active Fuchsia build.
  final FuchsiaLocator fuchsiaLocator;

  /// Translator between [TestEvent] instances and output for the user.
  final OutputFormatter outputFormatter;

  FuchsiaTestCommand({
    @required this.fuchsiaLocator,
    @required this.outputFormatter,
    @required this.testFlags,
  });

  Stream<TestEvent> get stream => _eventStreamController.stream;
  void emitEvent(TestEvent event) {
    _eventStreamController.sink.add(event);
  }

  void dispose() {
    _eventStreamController.sink.close();
  }

  Future<int> runTestSuite({TestsManifestReader manifestReader}) async {
    stream.listen(outputFormatter.update);

    manifestReader ??= TestsManifestReader();
    List<TestDefinition> testDefinitions = await manifestReader.loadTestsJson(
      buildDir: fuchsiaLocator.buildDir,
      manifestFileName: 'tests.json',
    );
    ParsedManifest parsedManifest = manifestReader.aggregateTests(
      buildDir: fuchsiaLocator.buildDir,
      eventEmitter: emitEvent,
      testDefinitions: testDefinitions,
      testFlags: testFlags,
    );
    manifestReader.reportOnTestRunners(
      userFriendlyBuildDir: fuchsiaLocator.userFriendlyBuildDir,
      eventEmitter: emitEvent,
      parsedManifest: parsedManifest,
      testFlags: testFlags,
    );
    var exitCode = 0;

    try {
      await _runTests(parsedManifest.testRunners).forEach((event) {
        emitEvent(event);
        if (event is TestResult && !event.isSuccess) {
          exitCode = 2;
        }
      });
    } on FailFastException catch (_) {
      exitCode = 2;
    }
    emitEvent(AllTestsCompleted());
    return exitCode;
  }

  Stream<TestEvent> _runTests(List<TestRunner> testRunners) async* {
    // Let the output formatter know that we're done parsing and
    // emitting preliminary events
    yield BeginningTests();

    var count = 0;
    for (TestRunner testRunner in testRunners) {
      yield* testRunner.run();

      count += 1;
      if (testFlags.limit > 0 && count >= testFlags.limit) {
        break;
      }
    }
  }
}
